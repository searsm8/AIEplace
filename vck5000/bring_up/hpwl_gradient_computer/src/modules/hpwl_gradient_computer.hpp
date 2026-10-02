#ifndef PL_ALGO_HPWL_GRADIENT_COMPUTER_HPP
#define PL_ALGO_HPWL_GRADIENT_COMPUTER_HPP

// hpwl_gradient_computer -- WA-wirelength gradient per node from the static pin-record stream (#41),
// one axis per call. Extends hpwl_computer_v2 (same on-chip gather, same Dhar bbox trees) with the
// rest of Dhar's Method 1, and does the pin->node summation as an on-chip scatter-add:
//
//   load_pos_zero_grad   positions -> pos_URAM and grad_URAM[movable slots] = 0, one pass
//   refresh_macros / load_offsets   as hpwl_computer_v2
//   beat_loop        three stages in a DATAFLOW region, joined by streams (one file each):
//                      pin_bbox     gather -> bbox trees -> each pin's net (max, min); HPWL out
//                      wa_sums      per-pin exp terms (Fig. 5) -> four sum trees (Fig. 6/7)
//                      wa_gradient  per-pin combiner (Fig. 8, eq. 4) -> merge -> scatter-add
//   fold_macros      grad[macro] = sum of its macro-pin slots' gradients
//   drain_grad       grad_URAM[movable slots] -> DDR, 16 slots per beat
//
// Same math as sw_only computeHpwlPartials_CPU (Partials.cpp) and hpwl_gradient_dhar: exponents
// shifted by the net's bbox, exp from the host LUT exp(-d/gamma) with linear interpolation.
//
// Large nets (17..96 pins, span 2..8 beats) need values from the net's later beats: A sends the
// net's bbox to B, and B sends the net's sums to C, each on its own stream, once per net. That makes
// the beat streams' depth a correctness bound (packer rule L8; see gradient_beat_loop).
//
// Each on-chip array has exactly one stage that touches it inside the region (pos/offset: pin_bbox,
// lut: wa_sums, grad: wa_gradient); the phases around the region stay sequential, because they
// share pos_URAM and grad_URAM. Meow.

#include "modules/wa_gradient.hpp"   // includes wa_sums, pin_bbox, hpwl_computer_v2

namespace plalgo {

// One copy per concurrent lookup: 32 lookups per beat, one read each. Meow.
static void load_exp_lut(const float* exp_lut_DDR, int lut_size, LutPair lut_BRAM[EXP_LOOKUPS][GRAD_LUT_MAX]) {
    float prev = 0.0f;
load_lut:
    for (int i = 0; i < lut_size; i++) {
#pragma HLS PIPELINE II=1
        const float value = exp_lut_DDR[i];
        if (i > 0)
            for (int copy = 0; copy < EXP_LOOKUPS; copy++) lut_BRAM[copy][i - 1] = LutPair{prev, value};
        prev = value;
    }
    for (int copy = 0; copy < EXP_LOOKUPS; copy++) lut_BRAM[copy][lut_size - 1] = LutPair{prev, 0.0f};   // never read: idx < lut_size-1
}

static void drain_slot_array(const float src_URAM[pinrec::BANKS][ROWS_PER_BANK], int num_slot_beats,
                             pinrec::SlotBeat* dst_DDR) {
drain_slots:
    for (int b = 0; b < num_slot_beats; b++) {
#pragma HLS PIPELINE II=1
        pinrec::SlotBeat beat;
        for (int j = 0; j < pinrec::LANES; j++)
            beat.v[j] = (b & 1) ? src_URAM[j + pinrec::LANES][b >> 1] : src_URAM[j][b >> 1];
        dst_DDR[b] = beat;
    }
}

// Macro-pin gradients fold into their macro: grad[macro] = sum of grad[its pin slots].
// The running sum lives in acc_URAM at the macro's slot, not in a register: a register chain
// carries a float add from one entry to the next, which HLS could only schedule at II=3 (4.9 ns
// path, measured 2026-09-22). In memory it is a read-add-write, and the host orders the list so a
// macro recurs only every HAZARD_DISTANCE entries (schedule_macro_pins) -- the same contract as the
// scatter-add. FIRST starts the sum; LAST writes it into grad. The caller passes pos_URAM as
// acc_URAM: positions are dead once the beat loop is done. The add keeps its default 1-cycle
// latency: it sits inside the read-add-write that HAZARD_DISTANCE covers. Meow.
static void fold_macro_pins(const pinrec::MacroPinRef* macro_pins_DDR, int num_macro_pins,
                            float grad_URAM[pinrec::BANKS][ROWS_PER_BANK],
                            float acc_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
fold_macros:
    for (int e = 0; e < num_macro_pins; e++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM inter false
#pragma HLS DEPENDENCE variable=acc_URAM type=inter direction=RAW distance=pinrec::HAZARD_DISTANCE dependent=true
        const pinrec::MacroPinRef ref = macro_pins_DDR[e];
        if (ref.pin_slot == pinrec::MACRO_PIN_SKIP) continue;
        const int macro_bank = ref.macro_slot % pinrec::BANKS, macro_row = ref.macro_slot / pinrec::BANKS;
        const float pin_grad = grad_URAM[ref.pin_slot % pinrec::BANKS][ref.pin_slot / pinrec::BANKS];
        const float sum = (ref.flags & pinrec::MACRO_PIN_FIRST) ? pin_grad : acc_URAM[macro_bank][macro_row] + pin_grad;
        if (ref.flags & pinrec::MACRO_PIN_LAST) grad_URAM[macro_bank][macro_row] = sum;
        else                                    acc_URAM[macro_bank][macro_row] = sum;
    }
}

// The record stream -> scatter-added gradient (and per-net HPWL), against positions already
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
                               const int beat_count_REG[pinrec::NET_DEGREES_PROCESSED],
                               const int span_count_REG[pinrec::SPAN_GROUPS],
                               const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],
                               const float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX],
                               const LutPair lut_BRAM[EXP_LOOKUPS][GRAD_LUT_MAX], int lut_size,
                               float inv_lut_step, float inv_gamma,
                               float grad_URAM[pinrec::BANKS][ROWS_PER_BANK], int first_fixed_slot,
                               OutBeat* out_beats_DDR, int offset_bits) {
#pragma HLS DATAFLOW // three stages working simultaneously, joined by streams
    hls::stream<PinBeat> pin_beats;  // stream between Stage A (pin_bbox) and Stage B (wa_sums)
    hls::stream<SumBeat> sum_beats;  // stream between Stage B (wa_sums) and Stage C (wa_gradient)
    hls::stream<NetBbox> net_bboxes; // A -> B, one per large net
    hls::stream<NetSum>  net_sums;   // B -> C, one per large net
#pragma HLS STREAM variable=pin_beats depth=BEAT_FIFO_DEPTH
#pragma HLS STREAM variable=sum_beats depth=BEAT_FIFO_DEPTH
#pragma HLS STREAM variable=net_bboxes depth=16
#pragma HLS STREAM variable=net_sums depth=16

    // Stage A: read record beats from DDR, gather positions from pos_URAM, compute each pin's bbox and HPWL, write HPWL to DDR
    pin_bbox(records_DDR, num_beats, beat_count_REG, span_count_REG, pos_URAM, offset_BRAM, out_beats_DDR, offset_bits,
             pin_beats, net_bboxes);

    // Stage B: read each pin's bbox, compute its exp terms, write four sum trees to sum_beats
    wa_sums(pin_beats, net_bboxes, num_beats, lut_BRAM, lut_size, inv_lut_step, sum_beats, net_sums);

    // Stage C: read each pin's sum trees, compute its gradient, scatter-add results into grad_URAM
    wa_gradient(sum_beats, net_sums, num_beats, inv_gamma, grad_URAM, first_fixed_slot);
}

static void hpwl_gradient_computer(
        const pinrec::RecordBeat*  records_DDR,       // [num_beats] static stream, this axis
        int                        num_beats,
        const int*                 beat_count_DDR,    // [NET_DEGREES_PROCESSED]
        const int*                 span_count_DDR,    // [SPAN_GROUPS] cumulative beats per large-net span
        const pinrec::SlotBeat*    pos_DDR,          // [num_slot_beats] slot-major positions, this axis
        int                        num_slot_beats,
        const pinrec::MacroPinRef* macro_pins_DDR,    // [num_macro_pins] refresh + fold list, this axis
        int                        num_macro_pins,
        const float*               offset_table_DDR,  // [offset_table_size] this axis
        int                        offset_table_size,
        const float*               exp_lut_DDR,       // [lut_size] exp(-t) table
        int                        lut_size,
        OutBeat*                   out_beats_DDR,     // [num_beats] per-net HPWL (by-product)
        pinrec::SlotBeat*          grad_DDR,          // [first_fixed_slot / LANES] gradient, movable slots
        int                        first_fixed_slot,
        int                        offset_bits,
        float                      inv_gamma,
        float                      inv_lut_step) {

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
cache_counts:
    for (int k = 0; k < pinrec::NET_DEGREES_PROCESSED; k++) beat_count_REG[k] = beat_count_DDR[k];
    int span_count_REG[pinrec::SPAN_GROUPS];
#pragma HLS ARRAY_PARTITION variable=span_count_REG complete dim=0
cache_spans:
    for (int k = 0; k < pinrec::SPAN_GROUPS; k++) span_count_REG[k] = span_count_DDR[k];

    const int movable_slot_beats = first_fixed_slot / pinrec::LANES;
    // load_slot_array for positions with the gradient zeroing folded in: grad_URAM is a separate
    // array, so its writes cost no extra cycles. first_fixed_slot is a multiple of BANKS, so the
    // movable beats are a prefix of the position beats. Meow.
load_pos_zero_grad:
    for (int b = 0; b < num_slot_beats; b++) {
#pragma HLS PIPELINE II=1
        const pinrec::SlotBeat beat = pos_DDR[b];
        const bool movable = b < movable_slot_beats;
        for (int j = 0; j < pinrec::LANES; j++) {
            if (b & 1) { // odd beat: banks LANES..2*LANES-1, row b>>1
                pos_URAM[j + pinrec::LANES][b >> 1] = beat.v[j];
                if (movable) grad_URAM[j + pinrec::LANES][b >> 1] = 0.0f;
            } else { // even beat: banks 0..LANES-1
                pos_URAM[j][b >> 1] = beat.v[j];
                if (movable) grad_URAM[j][b >> 1] = 0.0f;
            }
        }
    }
    refresh_macro_pins(macro_pins_DDR, num_macro_pins, pos_URAM);
    load_offset_table(offset_table_DDR, offset_table_size, offset_BRAM);
    load_exp_lut(exp_lut_DDR, lut_size, lut_BRAM);

    gradient_beat_loop(records_DDR, num_beats, beat_count_REG, span_count_REG, pos_URAM, offset_BRAM, lut_BRAM, lut_size,
                       inv_lut_step, inv_gamma, grad_URAM, first_fixed_slot, out_beats_DDR, offset_bits);

    fold_macro_pins(macro_pins_DDR, num_macro_pins, grad_URAM, pos_URAM);
    drain_slot_array(grad_URAM, movable_slot_beats, grad_DDR);
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_GRADIENT_COMPUTER_HPP
