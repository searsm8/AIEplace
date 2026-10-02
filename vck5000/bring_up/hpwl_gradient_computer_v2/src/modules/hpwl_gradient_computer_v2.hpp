#ifndef PL_ALGO_HPWL_GRADIENT_COMPUTER_V2_HPP
#define PL_ALGO_HPWL_GRADIENT_COMPUTER_V2_HPP

// hpwl_gradient_computer_v2 -- hpwl_gradient_computer extended to chunked designs (#41), the
// gradient counterpart of hpwl_computer_v3. Ghost POSITIONS go out through the exchange buffer as
// in v3; ghost GRADIENTS come back through the same buffer the other way:
//
//   export pass   for each chunk j: load positions, refresh macro pins, export ghost positions
//   compute pass  for each chunk k: load positions (zeroing grad in the same pass), refresh, import its ghosts, run the
//                 gradient beat loop, write its ghosts' gradients back into its own region, and
//                 drain its movable slots to grad_DDR
//   fold pass     for each chunk j: reload its gradient, add the ghost gradients other chunks
//                 computed for its nodes (its blocks of every region), fold macro pins, drain
//
// The macro fold waits for the fold pass because a macro pin can be a ghost elsewhere: its
// gradient is complete only once every chunk's contribution is in. The fold-pass accumulate is a
// read-add-write like the scatter-add; the host keeps a producer's slots HAZARD_DISTANCE entries
// apart in its block sequence (check_chunked verifies it). One chunk: no exchange, the macro fold
// runs in the compute pass, and this is exactly hpwl_gradient_computer.
//
// The exchange buffer is reused: chunk k's region is read (positions) before it is written
// (gradients), and only chunk k touches its region in the compute pass. Meow.

#include "modules/hpwl_gradient_computer.hpp"
#include "modules/hpwl_computer_v3.hpp"   // export_slot_values, import_ghost_values, cache_beat_counts

namespace plalgo {

// Consumer side, reversed: this chunk's ghost gradients, in region order. Meow.
static void export_ghost_gradients(const pinrec::ChunkDesc& desc, const int32_t* import_slots_DDR,
                                   const float grad_URAM[pinrec::BANKS][ROWS_PER_BANK], float* exchange_DDR) {
ghost_grads_out:
    for (int i = 0; i < desc.num_imports; i++) {
#pragma HLS PIPELINE II=1
        const int32_t slot = import_slots_DDR[desc.import_list_offset + i];
        exchange_DDR[desc.import_region + i] = grad_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS];
    }
}

// Producer side, reversed: add every consumer's gradient for this chunk's exported nodes. The
// host keeps a slot HAZARD_DISTANCE entries apart across the whole sequence, inserting padding
// entries (slot -1, skipped) where a block boundary would bring one closer. Meow.
static void add_ghost_gradients(const pinrec::ChunkDesc& desc,
                                const pinrec::ExchangeBlockRef* blocks_DDR, const int32_t* export_slots_DDR,
                                const float* exchange_DDR, float grad_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
    int entry = desc.export_list_offset;
ghost_grads_in:
    for (int k = 0; k < desc.num_export_blocks; k++) {
        const pinrec::ExchangeBlockRef block = blocks_DDR[desc.export_block_offset + k];
    ghost_grads_block:
        for (int i = 0; i < block.count; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM type=inter direction=RAW distance=pinrec::HAZARD_DISTANCE dependent=true
            const int32_t slot = export_slots_DDR[entry + i];
            if (slot < 0) continue;
            grad_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS] += exchange_DDR[block.offset + i];
        }
        entry += block.count;
    }
}

static void hpwl_gradient_computer_v2(
        const pinrec::ChunkDesc*        chunks_DDR,        // [num_chunks]
        int                             num_chunks,
        const pinrec::RecordBeat*       records_DDR,       // every chunk's stream, concatenated, this axis
        const pinrec::SlotBeat*         pos_DDR,           // every chunk's slot-major positions, this axis
        const pinrec::MacroPinRef*      macro_pins_DDR,    // every chunk's refresh / fold list, this axis
        const int32_t*                  import_slots_DDR,  // every chunk's ghost slots, region order
        const int32_t*                  export_slots_DDR,  // every chunk's exported own slots, block order
        const pinrec::ExchangeBlockRef* blocks_DDR,        // each chunk's segments (num_export_blocks)
        float*                          exchange_DDR,      // scratch: ghost positions, then ghost gradients
        const float*                    offset_table_DDR,  // [offset_table_size] shared by all chunks
        int                             offset_table_size,
        const float*                    exp_lut_DDR,       // [lut_size] exp(-t) table
        int                             lut_size,
        OutBeat*                        out_beats_DDR,     // per-net HPWL, same layout as records_DDR
        pinrec::SlotBeat*               grad_DDR,          // same layout as pos_DDR; movable slots written
        int                             offset_bits,
        float                           inv_gamma,
        float                           inv_lut_step) {

    ONCHIP_ARRAY float pos_URAM[pinrec::BANKS][ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=pos_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=pos_URAM type=ram_2p impl=uram
    ONCHIP_ARRAY float grad_URAM[pinrec::BANKS][ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=grad_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=grad_URAM type=ram_2p impl=uram
    ONCHIP_ARRAY float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX];
#pragma HLS ARRAY_PARTITION variable=offset_BRAM complete dim=1
    ONCHIP_ARRAY LutPair lut_BRAM[EXP_LOOKUPS][GRAD_LUT_MAX];
#pragma HLS ARRAY_PARTITION variable=lut_BRAM complete dim=1
    int beat_count_REG[pinrec::NET_DEGREES_PROCESSED];
#pragma HLS ARRAY_PARTITION variable=beat_count_REG complete dim=0

    const bool chunked = num_chunks > 1;
    load_offset_table(offset_table_DDR, offset_table_size, offset_BRAM);
    load_exp_lut(exp_lut_DDR, lut_size, lut_BRAM);

export_pass:
    for (int j = 0; j < num_chunks && chunked; j++) {
        const pinrec::ChunkDesc desc = chunks_DDR[j];
        load_slot_array(pos_DDR + desc.slot_beat_offset, desc.num_slot_beats, pos_URAM);
        refresh_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, pos_URAM);
        export_slot_values(desc, blocks_DDR, export_slots_DDR, pos_URAM, exchange_DDR);
    }

compute_pass:
    for (int k = 0; k < num_chunks; k++) {
        const pinrec::ChunkDesc desc = chunks_DDR[k];
        const int movable_slot_beats = desc.first_fixed_slot / pinrec::LANES;
        // Positions in with grad_URAM zeroed in the same pass (separate arrays, no extra cycles). Meow.
        const pinrec::SlotBeat* chunk_pos_DDR = pos_DDR + desc.slot_beat_offset;
    load_pos_zero_grad:
        for (int b = 0; b < desc.num_slot_beats; b++) {
#pragma HLS PIPELINE II=1
            const pinrec::SlotBeat beat = chunk_pos_DDR[b];
            const bool movable = b < movable_slot_beats;
            for (int j = 0; j < pinrec::LANES; j++) {
                if (b & 1) {
                    pos_URAM[j + pinrec::LANES][b >> 1] = beat.v[j];
                    if (movable) grad_URAM[j + pinrec::LANES][b >> 1] = 0.0f;
                } else {
                    pos_URAM[j][b >> 1] = beat.v[j];
                    if (movable) grad_URAM[j][b >> 1] = 0.0f;
                }
            }
        }
        refresh_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, pos_URAM);
        import_ghost_values(desc, import_slots_DDR, exchange_DDR, pos_URAM);
        cache_beat_counts(desc, beat_count_REG);
        gradient_beat_loop(records_DDR + desc.record_beat_offset, desc.num_beats, beat_count_REG, pos_URAM,
                           offset_BRAM, lut_BRAM, lut_size, inv_lut_step, inv_gamma, grad_URAM,
                           desc.first_fixed_slot, out_beats_DDR + desc.record_beat_offset, offset_bits);
        if (chunked) export_ghost_gradients(desc, import_slots_DDR, grad_URAM, exchange_DDR);
        else         fold_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, grad_URAM, pos_URAM);
        drain_slot_array(grad_URAM, movable_slot_beats, grad_DDR + desc.slot_beat_offset);
    }

fold_pass:
    for (int j = 0; j < num_chunks && chunked; j++) {
        const pinrec::ChunkDesc desc = chunks_DDR[j];
        const int movable_slot_beats = desc.first_fixed_slot / pinrec::LANES;
        load_slot_array(grad_DDR + desc.slot_beat_offset, movable_slot_beats, grad_URAM);
        add_ghost_gradients(desc, blocks_DDR, export_slots_DDR, exchange_DDR, grad_URAM);
        fold_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, grad_URAM, pos_URAM);
        drain_slot_array(grad_URAM, movable_slot_beats, grad_DDR + desc.slot_beat_offset);
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_GRADIENT_COMPUTER_V2_HPP
