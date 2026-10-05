#ifndef PL_ALGO_MAILBOX_WIDENED_HPP
#define PL_ALGO_MAILBOX_WIDENED_HPP

// mailbox_widened -- #42 prototype: hpwl_gradient_computer_v2's four mailbox loops (send / receive /
// return / collect), moving a 512-bit beat of 16 entries per cycle instead of one float. Host
// layout and its rules W1-W3: bring_up/mailbox_widened/mailbox_layout.hpp.
//
// Each loop reads one lane-list beat (16 slots, -1 = empty lane) and one mailbox beat per cycle,
// both sequential in DDR. The URAM side is bank-major, as in gather_pin_positions and the
// wa_gradient scatter-add: each of the 32 banks takes the at most one lane addressing it (W1/W2),
// so HLS sees one static access per bank per iteration. Meow.

#include "modules/hpwl_gradient_computer.hpp"   // ROWS_PER_BANK, pinrec, SlotBeat

namespace plalgo {

struct LaneBeat { int32_t slot[pinrec::LANES]; };   // one 512-bit DDR beat of a lane list

// Bank-major read of 16 lanes: bank b serves the lane addressing it; each lane muxes its bank. Meow.
static void gather_lanes(const LaneBeat& lanes, const float src_URAM[pinrec::BANKS][ROWS_PER_BANK], pinrec::SlotBeat& out) {
#pragma HLS INLINE
    float bank_value[pinrec::BANKS];
#pragma HLS ARRAY_PARTITION variable=bank_value complete dim=0
    for (int b = 0; b < pinrec::BANKS; b++) {   // 32 x (16:1 row mux + one URAM read)
        uint32_t row = 0;
        for (int i = 0; i < pinrec::LANES; i++)
            if (lanes.slot[i] >= 0 && lanes.slot[i] % pinrec::BANKS == b) row = lanes.slot[i] / pinrec::BANKS;
        bank_value[b] = src_URAM[b][row];
    }
    for (int i = 0; i < pinrec::LANES; i++) {   // 16 x 32:1 mux; masked loop, not bank_value[slot % BANKS] (see gather_pin_positions)
        float value = 0.0f;
        for (int b = 0; b < pinrec::BANKS; b++)
            if (lanes.slot[i] % pinrec::BANKS == b) value = bank_value[b];
        out.v[i] = value;
    }
}

// Bank-major write (or read-add-write when `accumulate`) of 16 lanes. Meow.
static void scatter_lanes(const LaneBeat& lanes, const pinrec::SlotBeat& in, float dst_URAM[pinrec::BANKS][ROWS_PER_BANK],
                          bool accumulate) {
#pragma HLS INLINE
    for (int b = 0; b < pinrec::BANKS; b++) {
        bool     hit = false;
        uint32_t row = 0;
        float    value = 0.0f;
        for (int i = 0; i < pinrec::LANES; i++)
            if (lanes.slot[i] >= 0 && lanes.slot[i] % pinrec::BANKS == b) { hit = true; row = lanes.slot[i] / pinrec::BANKS; value = in.v[i]; }
        if (hit) dst_URAM[b][row] = accumulate ? dst_URAM[b][row] + value : value;
    }
}

// Owner side: this chunk's shared positions into its parcels. Meow.
static void send_wide(const pinrec::ParcelRef* parcels_DDR, int num_parcels, const LaneBeat* shared_lanes_DDR,
                      const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK], pinrec::SlotBeat* mailbox_DDR) {
    int entry = 0;
send_parcels:
    for (int p = 0; p < num_parcels; p++) {
        const pinrec::ParcelRef parcel = parcels_DDR[p];
    send_beats:
        for (int b = 0; b < parcel.count; b++) {
#pragma HLS PIPELINE II=1
            const LaneBeat lanes = shared_lanes_DDR[entry + b];   // whole-beat copies: by reference, HLS reads 16 words. Meow.
            pinrec::SlotBeat beat;
            gather_lanes(lanes, pos_URAM, beat);
            mailbox_DDR[parcel.offset + b] = beat;
        }
        entry += parcel.count;
    }
}

// Consumer side: this chunk's inbox, one sequential run, into its external slots. Meow.
static void receive_wide(const LaneBeat* external_lanes_DDR, const pinrec::SlotBeat* inbox_DDR, int inbox_beats,
                         float pos_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
receive_beats:
    for (int b = 0; b < inbox_beats; b++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=pos_URAM inter false   // external slots are distinct across the whole inbox
        const LaneBeat lanes = external_lanes_DDR[b];
        const pinrec::SlotBeat beat = inbox_DDR[b];
        scatter_lanes(lanes, beat, pos_URAM, false);
    }
}

// Consumer side, reversed: its external slots' gradients back into its own inbox. Meow.
static void return_wide(const LaneBeat* external_lanes_DDR, const float grad_URAM[pinrec::BANKS][ROWS_PER_BANK],
                        pinrec::SlotBeat* inbox_DDR, int inbox_beats) {
return_beats:
    for (int b = 0; b < inbox_beats; b++) {
#pragma HLS PIPELINE II=1
        const LaneBeat lanes = external_lanes_DDR[b];
        pinrec::SlotBeat beat;
        gather_lanes(lanes, grad_URAM, beat);
        inbox_DDR[b] = beat;
    }
}

// Owner side, reversed: add every consumer's gradients for its shared slots. The host keeps a
// slot HAZARD_DISTANCE beats apart across the parcels (W3). Meow.
static void collect_wide(const pinrec::ParcelRef* parcels_DDR, int num_parcels, const LaneBeat* shared_lanes_DDR,
                         const pinrec::SlotBeat* mailbox_DDR, float grad_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
    int entry = 0;
collect_parcels:
    for (int p = 0; p < num_parcels; p++) {
        const pinrec::ParcelRef parcel = parcels_DDR[p];
    collect_beats:
        for (int b = 0; b < parcel.count; b++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM type=inter direction=RAW distance=pinrec::HAZARD_DISTANCE dependent=true
            const LaneBeat lanes = shared_lanes_DDR[entry + b];
            const pinrec::SlotBeat beat = mailbox_DDR[parcel.offset + b];
            scatter_lanes(lanes, beat, grad_URAM, true);
        }
        entry += parcel.count;
    }
}

} // namespace plalgo

#endif // PL_ALGO_MAILBOX_WIDENED_HPP
