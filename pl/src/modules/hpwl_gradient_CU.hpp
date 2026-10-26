#ifndef PL_HPWL_GRADIENT_CU_HPP
#define PL_HPWL_GRADIENT_CU_HPP

// hpwl_gradient_CU -- the WA-wirelength gradient per node (and the total HPWL as a by-product) from
// the static pin-record stream, one axis per call, for a design split into chunks of at most
// SLOT_CAPACITY on-chip slots (one chunk when it fits). Same math as sw_only
// computeHpwlPartials_CPU (Partials.cpp): exponents shifted by the net's bbox, exp from the host
// LUT exp(-d/gamma) with linear interpolation. Nets of 2..96 pins.
//
// Chunks. Every movable node has one OWNER chunk (packer rule C1). Each net is homed in one chunk;
// when it uses a node owned elsewhere, its chunk is a BORROWER of that node and keeps a copy of it
// in a BORROWED slot (a borrowed node, packer rule C7). A chunk's slots, low to high:
//
//     [0, first_private_slot)                    owned movable slots, every shared one among them (C8)
//     [first_private_slot, first_borrowed_slot)  owned movable slots no other chunk borrows
//     [first_borrowed_slot, first_fixed_slot)    borrowed rows: one block per owner it borrows from
//     [first_fixed_slot, num_slots)              fixed pins: position only, never a gradient
//
//   send_borrowed_pass   for each owner: load its shared rows' positions, write a copy into every
//                        borrower's borrowed block for it (borrowed_copies.hpp)
//   compute_pass         for each chunk: load all its positions (zeroing grad in the same pass),
//                        refresh macro pins, run the gradient beat loop, fold macro pins, and drain
//                        owned and borrowed gradients to grad_DDR
//   merge_borrowed_pass  for each owner: reload its shared rows' gradients, add in what every borrower
//                        drained into its borrowed block for it, drain those rows
// The two borrowed passes run only when the design is chunked. Meow.
//
// The beat loop is three stages in a DATAFLOW region, joined by streams (hpwl_gradient/):
//   hpwl_CU     gather -> bbox trees -> each pin's net (max, min); HPWL total
//   wa_sums      per-pin exp terms (Dhar Fig. 5) -> four sum trees (Fig. 6/7)
//   wa_gradient  per-pin combiner (Fig. 8, eq. 4) -> merge -> scatter-add
//
// A macro pin is never a borrowed slot (packer rule C6): a chunk using a pin of a macro owned elsewhere
// holds the macro in its borrowed rows and the pin as a local slot. So each chunk refreshes its pins
// after receive and folds them in compute_pass, its borrowed macros included; a macro's
// gradient then crosses chunks like a cell's, and only cells and macros cross (#47). Meow.
//
// Brought together 2026-10-06 (#45) from vck5000/bring_up/: hpwl_gradient_computer_v2 (this top,
// renamed) and hpwl_gradient_computer (gradient_beat_loop), @ 41f2193. Meow.

#include "modules/hpwl_gradient/slot_arrays.hpp"
#include "modules/hpwl_gradient/record_gather.hpp"
#include "modules/hpwl_gradient/wa_gradient.hpp"   // includes wa_sums, hpwl_CU
#include "modules/hpwl_gradient/borrowed_copies.hpp"

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
        pinrec::SlotBeat*               pos_DDR,           // every chunk's slot-major positions, this axis; send_borrowed_pass fills the borrowed rows
        const pinrec::MacroPinRef*      macro_pins_DDR,    // every chunk's refresh / fold list, this axis
        const pinrec::BorrowedBlockDesc* borrowed_blocks_DDR,   // every owner's borrowed blocks (ChunkDesc first_borrowed_block, num_borrowers)
        const uint32_t*                 borrowed_ops_DDR,      // their send and merge ops
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
#pragma HLS BIND_STORAGE variable=pos_URAM type=ram_2p impl=uram latency=2

    ONCHIP_ARRAY float grad_URAM[pinrec::BANKS][ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=grad_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=grad_URAM type=ram_2p impl=uram latency=2

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

    // For each owner: load the positions of its shared rows into URAM, then write a copy of every
    // node another chunk borrows into that borrower's borrowed block in DDR. After this, every
    // borrowed slot holds this iteration's position. Meow.
    if (chunked) {
    send_borrowed_pass:
        for (int j = 0; j < num_chunks; j++) {
            const pinrec::ChunkDesc desc = chunks_DDR[j];
            load_slot_array(pos_DDR + desc.slot_beat_offset, desc.first_private_slot / pinrec::LANES, pos_URAM);   // slot_arrays.hpp
            for (int s = 0; s < desc.num_borrowers; s++)
                send_borrowed_block(borrowed_blocks_DDR[desc.first_borrowed_block + s], borrowed_ops_DDR, pos_URAM, pos_DDR);   // borrowed_copies.hpp
        }
    }

    int64_t hpwl_sum = 0;
    // For each chunk: load all its positions (owned, borrowed, fixed) into URAM, compute every one of
    // its nets' gradient contributions, and write its owned and borrowed gradients to DDR, where
    // merge_borrowed_pass picks up the borrowed ones. Meow.
compute_pass:
    for (int k = 0; k < num_chunks; k++) {
        const pinrec::ChunkDesc desc = chunks_DDR[k];
        const int movable_slot_beats = desc.first_fixed_slot / pinrec::LANES;
        int64_t chunk_hpwl_fixed;
        load_pos_zero_grad(pos_DDR + desc.slot_beat_offset, desc.num_slot_beats, movable_slot_beats,   // slot_arrays.hpp
                           pos_URAM, grad_URAM);
        refresh_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, pos_URAM);   // slot_arrays.hpp; borrowed macros arrived with the borrowed rows (C6)
        load_group_ends(group_ends_DDR + k * pinrec::NUM_GROUP_ENDS, group_end_REG);   // record_gather.hpp
        gradient_beat_loop(records_DDR + desc.record_beat_offset, desc.num_beats, group_end_REG, pos_URAM,   // this file
                           offset_BRAM, lut_BRAM, lut_size, inv_lut_step, inv_gamma, grad_URAM,
                           desc.first_fixed_slot, offset_bits, chunk_hpwl_fixed);
        hpwl_sum += chunk_hpwl_fixed;
        // pos_URAM is passed as the fold's scratch accumulator: positions are dead once the beat loop is done. Meow.
        fold_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, grad_URAM, pos_URAM);   // slot_arrays.hpp
        drain_slot_array(grad_URAM, movable_slot_beats, grad_DDR + desc.slot_beat_offset);   // slot_arrays.hpp
    }
    hpwl_total_fixed = hpwl_sum;

    // For each owner: reload the gradients of its shared rows, add in what every borrower computed
    // in its borrowed slots for them, and write the totals back. compute_pass already drained the
    // private rows, which no borrower adds to. Meow.
    if (chunked) {
    merge_borrowed_pass:
        for (int j = 0; j < num_chunks; j++) {
            const pinrec::ChunkDesc desc = chunks_DDR[j];
            const int shared_slot_beats = desc.first_private_slot / pinrec::LANES;
            load_slot_array(grad_DDR + desc.slot_beat_offset, shared_slot_beats, grad_URAM);   // slot_arrays.hpp
            for (int s = 0; s < desc.num_borrowers; s++)   // borrowers ascending: each slot's adds in a fixed order
                merge_borrowed_block(borrowed_blocks_DDR[desc.first_borrowed_block + s], borrowed_ops_DDR, grad_DDR, grad_URAM);   // borrowed_copies.hpp
            drain_slot_array(grad_URAM, shared_slot_beats, grad_DDR + desc.slot_beat_offset);   // slot_arrays.hpp
        }
    }
}

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_CU_HPP
