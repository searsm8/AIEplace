#ifndef PL_ALGO_HPWL_COMPUTER_V3_HPP
#define PL_ALGO_HPWL_COMPUTER_V3_HPP

// hpwl_computer_v3 -- hpwl_computer_v2 extended to designs larger than one on-chip slot space (#41).
//
// The host splits the design into chunks (beat_packer.hpp encode_chunked): each chunk owns some
// nodes, carries the nets homed in it as an ordinary record stream over its own local slots, and
// holds GHOST slots for nodes owned by other chunks. Ghost positions travel through one DDR
// exchange buffer, consumer-major (chunk k's region = its ghosts, one block per producer).
//
//   export pass    for each chunk j: load its positions, refresh its macro pins, write the
//                  positions other chunks need into their regions (one sequential block each)
//   compute pass   for each chunk k: load its positions, refresh, read its region sequentially
//                  into its ghost slots, then run hpwl_computer_v2's beat loop on its stream
//
// Every DDR access is still sequential -- blocks, regions, descriptors, streams -- and every
// random access is still on chip. One chunk means nothing to exchange, so the export pass is
// skipped and this is exactly hpwl_computer_v2. Meow.

#include "modules/hpwl_computer_v2.hpp"

namespace plalgo {

// Producer side: this chunk's positions that other chunks hold as ghosts, segment by segment.
// Padding entries (slot -1, only there to space the gradient return) write nothing. Meow.
static void export_slot_values(const pinrec::ChunkDesc& desc,
                               const pinrec::ExchangeBlockRef* blocks_DDR, const int32_t* export_slots_DDR,
                               const float src_URAM[pinrec::BANKS][ROWS_PER_BANK], float* exchange_DDR) {
    int entry = desc.export_list_offset;
export_blocks:
    for (int k = 0; k < desc.num_export_blocks; k++) {
        const pinrec::ExchangeBlockRef block = blocks_DDR[desc.export_block_offset + k];
    export_block:
        for (int i = 0; i < block.count; i++) {
#pragma HLS PIPELINE II=1
            const int32_t slot = export_slots_DDR[entry + i];
            if (slot < 0) continue;
            exchange_DDR[block.offset + i] = src_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS];
        }
        entry += block.count;
    }
}

// Consumer side: this chunk's region, in order, into its ghost slots (all distinct). Meow.
static void import_ghost_values(const pinrec::ChunkDesc& desc, const int32_t* import_slots_DDR,
                                const float* exchange_DDR, float dst_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
import_ghosts:
    for (int i = 0; i < desc.num_imports; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=dst_URAM inter false
        const int32_t slot = import_slots_DDR[desc.import_list_offset + i];
        dst_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS] = exchange_DDR[desc.import_region + i];
    }
}

static void cache_beat_counts(const pinrec::ChunkDesc& desc, int beat_count_REG[pinrec::NET_DEGREES_PROCESSED]) {
#pragma HLS INLINE
    for (int k = 0; k < pinrec::NET_DEGREES_PROCESSED; k++) beat_count_REG[k] = desc.beat_count[k];
}

static void hpwl_computer_v3(
        const pinrec::ChunkDesc*        chunks_DDR,        // [num_chunks]
        int                             num_chunks,
        const pinrec::RecordBeat*       records_DDR,       // every chunk's stream, concatenated, this axis
        const pinrec::SlotBeat*         pos_DDR,           // every chunk's slot-major positions, this axis
        const pinrec::MacroPinRef*      macro_pins_DDR,    // every chunk's refresh list, this axis
        const int32_t*                  import_slots_DDR,  // every chunk's ghost slots, region order
        const int32_t*                  export_slots_DDR,  // every chunk's exported own slots, block order
        const pinrec::ExchangeBlockRef* blocks_DDR,        // each chunk's segments (num_export_blocks)
        float*                          exchange_DDR,      // scratch: ghost positions, consumer-major
        const float*                    offset_table_DDR,  // [offset_table_size] shared by all chunks
        int                             offset_table_size,
        OutBeat*                        out_beats_DDR,     // per-net HPWL, same layout as records_DDR
        int                             offset_bits) {

    ONCHIP_ARRAY float pos_URAM[pinrec::BANKS][ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=pos_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=pos_URAM type=ram_2p impl=uram
    ONCHIP_ARRAY float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX];
#pragma HLS ARRAY_PARTITION variable=offset_BRAM complete dim=1
    int beat_count_REG[pinrec::NET_DEGREES_PROCESSED];
#pragma HLS ARRAY_PARTITION variable=beat_count_REG complete dim=0
    int span_count_REG[pinrec::SPAN_GROUPS];
#pragma HLS ARRAY_PARTITION variable=span_count_REG complete dim=0

    load_offset_table(offset_table_DDR, offset_table_size, offset_BRAM);

export_pass:
    for (int j = 0; j < num_chunks && num_chunks > 1; j++) {
        const pinrec::ChunkDesc desc = chunks_DDR[j];
        load_slot_array(pos_DDR + desc.slot_beat_offset, desc.num_slot_beats, pos_URAM);
        refresh_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, pos_URAM);
        export_slot_values(desc, blocks_DDR, export_slots_DDR, pos_URAM, exchange_DDR);
    }

compute_pass:
    for (int k = 0; k < num_chunks; k++) {
        const pinrec::ChunkDesc desc = chunks_DDR[k];
        load_slot_array(pos_DDR + desc.slot_beat_offset, desc.num_slot_beats, pos_URAM);
        refresh_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, pos_URAM);
        import_ghost_values(desc, import_slots_DDR, exchange_DDR, pos_URAM);
        cache_beat_counts(desc, beat_count_REG);
        // Chunks carry no large nets (only 2..16-pin nets are homed), so every span group is empty. Meow.
        for (int i = 0; i < pinrec::SPAN_GROUPS; i++) span_count_REG[i] = desc.num_beats;
        hpwl_beat_loop(records_DDR + desc.record_beat_offset, desc.num_beats, beat_count_REG, span_count_REG, pos_URAM,
                       offset_BRAM, out_beats_DDR + desc.record_beat_offset, offset_bits);
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_COMPUTER_V3_HPP
