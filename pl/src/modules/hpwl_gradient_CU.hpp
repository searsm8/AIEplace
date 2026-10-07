#ifndef PL_HPWL_GRADIENT_CU_HPP
#define PL_HPWL_GRADIENT_CU_HPP

// hpwl_gradient_CU -- the WA-wirelength gradient per node (and the total HPWL as a by-product) from
// the static pin-record stream, one axis per call, for a design split into chunks of at most
// SLOT_CAPACITY on-chip slots (one chunk when it fits). Same math as sw_only
// computeHpwlPartials_CPU (Partials.cpp): exponents shifted by the net's bbox, exp from the host
// LUT exp(-d/gamma) with linear interpolation. Nets of 2..96 pins.
//
//   send pass     for each chunk j: load positions, refresh macro pins, send external positions
//   compute pass  for each chunk k: load positions (zeroing grad in the same pass), refresh,
//                 receive its external positions, run the gradient beat loop, return its external
//                 slots' gradients into its own inbox, and drain its movable slots to grad_DDR
//   fold pass     for each chunk j: reload its gradient, collect the external gradients other
//                 chunks computed for its nodes, fold macro pins, drain
//
// The beat loop is three stages in a DATAFLOW region, joined by streams (hpwl_gradient/):
//   hpwl_CU     gather -> bbox trees -> each pin's net (max, min); HPWL total
//   wa_sums      per-pin exp terms (Dhar Fig. 5) -> four sum trees (Fig. 6/7)
//   wa_gradient  per-pin combiner (Fig. 8, eq. 4) -> merge -> scatter-add
//
// The macro fold waits for the fold pass because a macro pin can be external elsewhere: its
// gradient is complete only once every chunk's contribution is in. One chunk: no mailbox, and the
// macro fold runs in the compute pass.
//
// Brought together 2026-10-06 (#45) from vck5000/bring_up/: hpwl_gradient_computer_v2 (this top,
// renamed) and hpwl_gradient_computer (gradient_beat_loop), @ 41f2193. Meow.

#include "modules/hpwl_gradient/slot_arrays.hpp"
#include "modules/hpwl_gradient/record_gather.hpp"
#include "modules/hpwl_gradient/wa_gradient.hpp"   // includes wa_sums, hpwl_CU
#include "modules/hpwl_gradient/mailbox.hpp"

namespace plalgo {

// The record stream -> scatter-added gradient (and the chunk's total HPWL), against positions already
// resident in pos_URAM and a grad_URAM the caller has zeroed.
// Deadlock bound: B reads a large net's first beat, then blocks on net_bboxes until A has finished
// the net's last real beat; likewise C on net_sums. A writes the net's bbox `skew` pipeline states
// AFTER its beat write (the window tree sits between them), and a stalled pipeline stalls whole, so
// by the time the bbox write is reached A has pushed `skew` beats of the NEXT net too:
//     depth >= extent - 1 + skew      (extent <= MAX_NET_EXTENT, packer rule L8)
// Measured skew at 3.33 ns (csynth 2026-10-02): A 6 states (pin_beats @20, net_bboxes @26), B 7
// (sum_beats @37, net_sums @44). Co-sim confirms the formula on a net of extent 13: depth 19 passes,
// 18 deadlocks on sum_beats (cosim/cosim_depth.tcl). Depth 2*MAX_NET_EXTENT leaves 17 states of skew
// margin at no extra cost: above 16, HLS puts each beat FIFO in 29 BRAM18, the same at 19 and 32.
// The per-net streams only absorb how many nets a stage runs ahead of the next. Meow.
#ifndef PL_BEAT_FIFO_DEPTH
#define PL_BEAT_FIFO_DEPTH (2 * pinrec::MAX_NET_EXTENT)   // override (-D) only for the co-sim depth sweep
#endif
constexpr int BEAT_FIFO_DEPTH = PL_BEAT_FIFO_DEPTH;

static void gradient_beat_loop(const pinrec::RecordBeat* records_DDR, int num_beats,
                               const int group_end_REG[pinrec::NUM_GROUP_ENDS],
                               const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],
                               const float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX],
                               const LutPair lut_BRAM[EXP_LOOKUPS][GRAD_LUT_MAX], int lut_size,
                               float inv_lut_step, float inv_gamma,
                               float grad_URAM[pinrec::BANKS][ROWS_PER_BANK], int first_fixed_slot,
                               int offset_bits, int64_t& chunk_hpwl_fixed) {
#pragma HLS DATAFLOW // three stages working simultaneously, joined by streams
    hls::stream<PinBeat> pin_beats;  // stream between Stage A (hpwl_CU) and Stage B (wa_sums)
    hls::stream<SumBeat> sum_beats;  // stream between Stage B (wa_sums) and Stage C (wa_gradient)
    hls::stream<NetBbox> net_bboxes; // A -> B, one per large net
    hls::stream<NetSum>  net_sums;   // B -> C, one per large net
#pragma HLS STREAM variable=pin_beats depth=BEAT_FIFO_DEPTH
#pragma HLS STREAM variable=sum_beats depth=BEAT_FIFO_DEPTH
#pragma HLS STREAM variable=net_bboxes depth=16
#pragma HLS STREAM variable=net_sums depth=16

    // Stage A: read record beats from DDR, gather positions from pos_URAM, compute each pin's bbox, sum the nets' HPWL
    hpwl_CU(records_DDR, num_beats, group_end_REG, pos_URAM, offset_BRAM, offset_bits,   // hpwl_CU.hpp
            pin_beats, net_bboxes, chunk_hpwl_fixed);

    // Stage B: read each pin's bbox, compute its exp terms, write four sum trees to sum_beats
    wa_sums(pin_beats, net_bboxes, num_beats, lut_BRAM, lut_size, inv_lut_step, sum_beats, net_sums);   // wa_sums.hpp

    // Stage C: read each pin's sum trees, compute its gradient, scatter-add results into grad_URAM
    wa_gradient(sum_beats, net_sums, num_beats, inv_gamma, grad_URAM, first_fixed_slot);   // wa_gradient.hpp
}

static void hpwl_gradient_CU(
        const pinrec::ChunkDesc*        chunks_DDR,        // [num_chunks]
        const int32_t*                  group_ends_DDR,  // [num_chunks * NUM_GROUP_ENDS]
        int                             num_chunks,
        const pinrec::RecordBeat*       records_DDR,       // every chunk's stream, concatenated, this axis
        const pinrec::SlotBeat*         pos_DDR,           // every chunk's slot-major positions, this axis
        const pinrec::MacroPinRef*      macro_pins_DDR,    // every chunk's refresh / fold list, this axis
        const int32_t*                  external_slots_DDR, // every chunk's external slots, inbox order
        const int32_t*                  shared_slots_DDR,  // every chunk's shared own slots, parcel order
        const pinrec::ParcelRef*        parcels_DDR,       // each chunk's segments (num_parcels)
        float*                          mailbox_DDR,       // scratch: external positions, then external gradients
        const float*                    offset_table_DDR,  // [offset_table_size] shared by all chunks
        int                             offset_table_size,
        const float*                    exp_lut_DDR,       // [lut_size] exp(-t) table
        int                             lut_size,
        pinrec::SlotBeat*               grad_DDR,          // same layout as pos_DDR; movable slots written
        int                             offset_bits,
        float                           inv_gamma,
        float                           inv_lut_step,
        int64_t&                        hpwl_total_fixed) { // this axis, pinrec::hpwl_to_fixed units

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

    // control registers for the beat loop, loaded once per chunk
    int group_end_REG[pinrec::NUM_GROUP_ENDS];
#pragma HLS ARRAY_PARTITION variable=group_end_REG complete dim=0

    const bool chunked = num_chunks > 1;
    load_offset_table(offset_table_DDR, offset_table_size, offset_BRAM);   // record_gather.hpp
    load_exp_lut(exp_lut_DDR, lut_size, lut_BRAM);   // wa_sums.hpp

send_pass:
    for (int j = 0; j < num_chunks && chunked; j++) {
        const pinrec::ChunkDesc desc = chunks_DDR[j];
        load_slot_array(pos_DDR + desc.slot_beat_offset, desc.num_slot_beats, pos_URAM);   // slot_arrays.hpp
        refresh_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, pos_URAM);   // slot_arrays.hpp
        send_external_positions(desc, parcels_DDR, shared_slots_DDR, pos_URAM, mailbox_DDR);   // mailbox.hpp
    }

    int64_t hpwl_sum = 0;
compute_pass:
    for (int k = 0; k < num_chunks; k++) {
        const pinrec::ChunkDesc desc = chunks_DDR[k];
        const int movable_slot_beats = desc.first_fixed_slot / pinrec::LANES;
        int64_t chunk_hpwl_fixed;
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
        refresh_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, pos_URAM);   // slot_arrays.hpp
        receive_external_positions(desc, external_slots_DDR, mailbox_DDR, pos_URAM);   // mailbox.hpp
        load_group_ends(group_ends_DDR + k * pinrec::NUM_GROUP_ENDS, group_end_REG);   // record_gather.hpp
        gradient_beat_loop(records_DDR + desc.record_beat_offset, desc.num_beats, group_end_REG, pos_URAM,   // this file
                           offset_BRAM, lut_BRAM, lut_size, inv_lut_step, inv_gamma, grad_URAM,
                           desc.first_fixed_slot, offset_bits, chunk_hpwl_fixed);
        hpwl_sum += chunk_hpwl_fixed;
        if (chunked) return_external_gradients(desc, external_slots_DDR, grad_URAM, mailbox_DDR);   // mailbox.hpp
        else         fold_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, grad_URAM, pos_URAM);   // slot_arrays.hpp
        drain_slot_array(grad_URAM, movable_slot_beats, grad_DDR + desc.slot_beat_offset);   // slot_arrays.hpp
    }
    hpwl_total_fixed = hpwl_sum;

fold_pass:
    for (int j = 0; j < num_chunks && chunked; j++) {
        const pinrec::ChunkDesc desc = chunks_DDR[j];
        const int movable_slot_beats = desc.first_fixed_slot / pinrec::LANES;
        load_slot_array(grad_DDR + desc.slot_beat_offset, movable_slot_beats, grad_URAM);   // slot_arrays.hpp
        collect_external_gradients(desc, parcels_DDR, shared_slots_DDR, mailbox_DDR, grad_URAM);   // mailbox.hpp
        fold_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, grad_URAM, pos_URAM);   // slot_arrays.hpp
        drain_slot_array(grad_URAM, movable_slot_beats, grad_DDR + desc.slot_beat_offset);   // slot_arrays.hpp
    }
}

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_CU_HPP
