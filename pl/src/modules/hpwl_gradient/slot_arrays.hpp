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
//
// Load pos_URAM, one beat (16 floats, half a row) per cycle, zeroing grad_URAM's movable slots in
// the same cycle: separate arrays, so the zeroing costs no extra cycles. Meow.
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
                pos_URAM[j + pinrec::LANES][b >> 1] = beat.v[j]; // load pos_URAM with chunk
                if (movable) grad_URAM[j + pinrec::LANES][b >> 1] = 0.0f; // zero out all movable entries in grad_URAM
            } else {
                pos_URAM[j][b >> 1] = beat.v[j];
                if (movable) grad_URAM[j][b >> 1] = 0.0f;
            }
        }
    }
}

static void drain_slot_array(const float src_URAM[pinrec::BANKS][ROWS_PER_BANK], int num_slot_beats,
                             pinrec::SlotBeat* dst_DDR) {
    // The reverse of load_pos_zero_grad: URAM -> DDR, one beat per cycle. Meow.
drain_slots:
    for (int b = 0; b < num_slot_beats; b++) {
#pragma HLS PIPELINE II=1
        pinrec::SlotBeat beat;
        for (int j = 0; j < pinrec::LANES; j++)
            beat.v[j] = (b & 1) ? src_URAM[j + pinrec::LANES][b >> 1] : src_URAM[j][b >> 1];
        dst_DDR[b] = beat;
    }
}

static_assert(ROWS_PER_BANK - 1 <= pinrec::MACRO_ROW_MASK, "a MacroBeat pin_row holds a URAM row in 15 bits");

// A macro beat's 16 lanes summed by a fixed balanced tree, empty lanes 0.0f:
// ((g0+g1)+(g2+g3)) + ((g4+g5)+(g6+g7)), likewise 8..15, then the two halves. sw_flat's golden
// (record_stream.cpp) calls this same function, so this order IS the reference order. Meow.
static float macro_pin_tree(const float lane_grad[pinrec::LANES]) {
#pragma HLS INLINE
    static_assert(pinrec::LANES == 16, "macro_pin_tree is a 4-level tree");
    float level[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=level complete dim=0
    for (int i = 0; i < pinrec::LANES; i++) level[i] = lane_grad[i];
    // Level by level, in place: entry i of the next level reads entries 2i and 2i+1, never one
    // already overwritten at this level. 15 adds, 4 deep. Meow.
    for (int width = pinrec::LANES / 2; width >= 1; width /= 2)
        for (int i = 0; i < width; i++) level[i] = level[2 * i] + level[2 * i + 1];
    return level[0];
}

static void refresh_macro_pins(const pinrec::MacroBeat* macro_beats_DDR, const pinrec::SlotBeat* macro_offsets_DDR,
                               int num_macro_beats, float pos_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
    // Set each macro pin's position to its macro's position plus the pin's offset, so the nets see
    // the pin where the macro is now: up to 16 pins of one macro per beat. Meow.
refresh_macros:   // reads macro slots, writes macro-pin slots: disjoint, so no carried dependence
    for (int t = 0; t < num_macro_beats; t++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=pos_URAM inter false // tells HLS the read and write are disjoint, so it can schedule II=1
        const pinrec::MacroBeat beat = macro_beats_DDR[t];
        const pinrec::SlotBeat  lane_offset = macro_offsets_DDR[t];   // a second port: both words arrive in one cycle
        if (beat.macro_slot == pinrec::MACRO_BEAT_SKIP) continue;   // fold-schedule padding
        const float macro_pos = pos_URAM[beat.macro_slot % pinrec::BANKS][beat.macro_slot / pinrec::BANKS];
        float pin_pos[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=pin_pos complete
        for (int lane = 0; lane < pinrec::LANES; lane++) {
            const float lane_pos = macro_pos + lane_offset.v[lane];
            // A binding unlike the fold's keeps HLS from sharing adders between the two loops:
            // shared, one adder's output fed every bank of pos, grad and acc, the worst routed
            // paths of 2026-09-23. The extra stage is free here (no carried dependence). Meow.
#pragma HLS BIND_OP variable=lane_pos op=fadd impl=primitivedsp latency=2
            pin_pos[lane] = lane_pos;
        }
        // Bank-major write: bank b takes lane b % LANES when that lane's pin is in b's half, so each
        // bank sees a 2:1 choice, not a 16-way crossbar. Meow.
        for (int bank = 0; bank < pinrec::BANKS; bank++) {
            const int lane = bank % pinrec::LANES;
            const bool upper = (beat.pin_row[lane] & pinrec::MACRO_ROW_UPPER) != 0;
            if ((beat.lane_valid >> lane & 1) && upper == (bank >= pinrec::LANES))
                pos_URAM[bank][beat.pin_row[lane] & pinrec::MACRO_ROW_MASK] = pin_pos[lane];
        }
    }
}

// Macro-pin gradients fold into their macro: grad[macro] = sum over its beats of macro_pin_tree.
// The running sum lives in acc_URAM at the macro's slot, not in a register: a register chain
// carries a float add from one beat to the next, which HLS could only schedule at II=3 (4.9 ns
// path, measured 2026-09-22). In memory it is a read-add-write, and the host orders the beats so a
// macro recurs only every MACRO_FOLD_DISTANCE beats (schedule_macro_pins). FIRST starts the sum;
// LAST writes it into grad. The caller passes pos_URAM as acc_URAM: positions are dead once the beat
// loop is done. Only the add into acc sits inside the read-add-write that the distance covers; the
// tree before it carries nothing between beats. At distance 8 the add takes 3 cycles: at 4 it had to
// be a 1-cycle add chained with the URAM read and bank mux (4.3 ns estimated, 2026-10-27). Its
// binding (fulldsp; refresh_macros uses primitivedsp) keeps HLS from sharing it: shared with B3's
// merge (removed 2026-10-29, #47 D), one adder fed the write data of every bank of both arrays, the
// largest failing group at 250 MHz (2026-10-26). Meow.
static void fold_macro_pins(const pinrec::MacroBeat* macro_beats_DDR, int num_macro_beats,
                            float grad_URAM[pinrec::BANKS][ROWS_PER_BANK],
                            float acc_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
    // Sum each macro's pin gradients into the macro's gradient: the macro moves as one body. Meow.
fold_macros:
    for (int t = 0; t < num_macro_beats; t++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM inter false
#pragma HLS DEPENDENCE variable=acc_URAM type=inter direction=RAW distance=pinrec::MACRO_FOLD_DISTANCE dependent=true
        const pinrec::MacroBeat beat = macro_beats_DDR[t];
        if (beat.macro_slot == pinrec::MACRO_BEAT_SKIP) continue;
        // Bank-major read: bank b reads the row lane b % LANES names, then each lane takes the bank
        // of its half. Pin slots are never macro slots, so these reads never see this loop's writes. Meow.
        float bank_grad[pinrec::BANKS];
#pragma HLS ARRAY_PARTITION variable=bank_grad complete
        for (int bank = 0; bank < pinrec::BANKS; bank++)
            bank_grad[bank] = grad_URAM[bank][beat.pin_row[bank % pinrec::LANES] & pinrec::MACRO_ROW_MASK];
        float lane_grad[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=lane_grad complete
        for (int lane = 0; lane < pinrec::LANES; lane++) {
            const bool upper = (beat.pin_row[lane] & pinrec::MACRO_ROW_UPPER) != 0;
            lane_grad[lane] = !(beat.lane_valid >> lane & 1) ? 0.0f : bank_grad[upper ? lane + pinrec::LANES : lane];
        }
        const float beat_sum = macro_pin_tree(lane_grad);
        const int macro_bank = beat.macro_slot % pinrec::BANKS, macro_row = beat.macro_slot / pinrec::BANKS;
        const float acc_plus_beat = acc_URAM[macro_bank][macro_row] + beat_sum;
#pragma HLS BIND_OP variable=acc_plus_beat op=fadd impl=primitivedsp latency=3
        const float sum = (beat.flags & pinrec::MACRO_BEAT_FIRST) ? beat_sum : acc_plus_beat;
        if (beat.flags & pinrec::MACRO_BEAT_LAST) grad_URAM[macro_bank][macro_row] = sum;
        else                                       acc_URAM[macro_bank][macro_row] = sum;
    }
}

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_SLOT_ARRAYS_HPP
