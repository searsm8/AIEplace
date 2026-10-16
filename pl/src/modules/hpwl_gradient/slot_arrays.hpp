#ifndef PL_HPWL_GRADIENT_SLOT_ARRAYS_HPP
#define PL_HPWL_GRADIENT_SLOT_ARRAYS_HPP

// slot_arrays -- the on-chip slot arrays (pos_URAM, grad_URAM: 32 banks, slot = row * BANKS + bank)
// and the sequential passes that fill, patch and empty them: load / drain 16 slots per DDR beat,
// refresh macro-pin positions from their macro, fold macro-pin gradients back into their macro.
// From vck5000/bring_up/hpwl_computer_v2 and hpwl_gradient_computer (modules/*.hpp) @ 41f2193 (#45). Meow.

#include "format/pin_record.hpp"

namespace plalgo {

// On-chip arrays too big for a software stack are static in tier-1 builds only. Under synthesis
// they are plain locals: a static local would mean state that persists across kernel calls (and
// an initial value to materialize), which no caller relies on. (Suspected first, wrongly, of the
// front-end hang that the gather readout turned out to cause -- see gather_pin_positions.) Meow.
#ifdef __SYNTHESIS__
#define ONCHIP_ARRAY
#else
#define ONCHIP_ARRAY static
#endif

#ifndef PL_SLOT_CAPACITY
#define PL_SLOT_CAPACITY (1 << 20)   // override (-D) only for synthesis experiments
#endif
constexpr int SLOT_CAPACITY    = PL_SLOT_CAPACITY;               // on-chip slots per array
constexpr int ROWS_PER_BANK    = SLOT_CAPACITY / pinrec::BANKS;   // 32K

// Slot-major DDR beat b holds slots 16b..16b+15 -> banks (b&1)*16 + j, row b>>1: sixteen distinct
// banks, so a whole beat lands in one cycle. Shared by every array loaded or drained this way. Meow.
static void load_slot_array(const pinrec::SlotBeat* src_DDR, int num_slot_beats,
                            float dst_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
    // Copy a chunk's array DDR -> URAM, one beat (16 floats, half a row) per cycle. Meow.
load_slots:
    for (int b = 0; b < num_slot_beats; b++) {
#pragma HLS PIPELINE II=1
        const pinrec::SlotBeat beat = src_DDR[b];
        for (int j = 0; j < pinrec::LANES; j++) {
            if (b & 1) dst_URAM[j + pinrec::LANES][b >> 1] = beat.v[j];
            else       dst_URAM[j][b >> 1]                 = beat.v[j];
        }
    }
}

// load_slot_array for pos_URAM, zeroing grad_URAM's movable slots in the same cycle: separate
// arrays, so the zeroing costs no extra cycles. Meow.
static void load_pos_zero_grad(const pinrec::SlotBeat* pos_DDR, int num_slot_beats, int movable_slot_beats,
                               float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],
                               float grad_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
load_pos_zero_grad:
    for (int b = 0; b < num_slot_beats; b++) {
#pragma HLS PIPELINE II=1
        const pinrec::SlotBeat beat = pos_DDR[b];
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
}

static void drain_slot_array(const float src_URAM[pinrec::BANKS][ROWS_PER_BANK], int num_slot_beats,
                             pinrec::SlotBeat* dst_DDR) {
    // The reverse of load_slots: URAM -> DDR, one beat per cycle. Meow.
drain_slots:
    for (int b = 0; b < num_slot_beats; b++) {
#pragma HLS PIPELINE II=1
        pinrec::SlotBeat beat;
        for (int j = 0; j < pinrec::LANES; j++)
            beat.v[j] = (b & 1) ? src_URAM[j + pinrec::LANES][b >> 1] : src_URAM[j][b >> 1];
        dst_DDR[b] = beat;
    }
}

static void refresh_macro_pins(const pinrec::MacroPinRef* macro_pins_DDR, int num_macro_pins,
                               float pos_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
    // Set each macro pin's position to its macro's position plus the pin's offset, so the nets see
    // the pin where the macro is now. Meow.
refresh_macros:   // reads macro slots, writes macro-pin slots: disjoint, so no carried dependence
    for (int e = 0; e < num_macro_pins; e++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=pos_URAM inter false // tells HLS the read and write are disjoint, so it can schedule II=1
        const pinrec::MacroPinRef ref = macro_pins_DDR[e];
        if (ref.pin_slot == pinrec::MACRO_PIN_SKIP) continue;   // fold-schedule padding
        const float macro_pos = pos_URAM[ref.macro_slot % pinrec::BANKS][ref.macro_slot / pinrec::BANKS];
        const float pin_pos = macro_pos + ref.offset;
        // A binding unlike fold_macro_pins' keeps HLS from sharing one adder between the two loops:
        // shared, its output fed every bank of pos, grad and acc, the worst routed paths of
        // 2026-09-23. The extra stage is free here (no carried dependence) and gives Vivado a
        // register to replicate toward the banks. Meow.
#pragma HLS BIND_OP variable=pin_pos op=fadd impl=primitivedsp latency=2
        pos_URAM[ref.pin_slot % pinrec::BANKS][ref.pin_slot / pinrec::BANKS] = pin_pos;
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
    // Sum each macro's pin gradients into the macro's gradient: the macro moves as one body. Meow.
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

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_SLOT_ARRAYS_HPP
