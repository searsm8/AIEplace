#ifndef PL_HPWL_GRADIENT_RECORD_GATHER_HPP
#define PL_HPWL_GRADIENT_RECORD_GATHER_HPP

// record_gather -- reading the static pin-record stream (format/pin_record.hpp): where a beat sits
// (degree group, large-net span, from the per-chunk group ends), decoding its 16 records, and the
// bank-major gather of 16 pin positions (node position + pin offset) from pos_URAM. Also the
// window reduction a large net uses to combine its beats.
// From vck5000/bring_up/hpwl_computer_v2 and hpwl_computer_v3 (modules/*.hpp) @ 41f2193 (#45).
//
// The host guarantees the 16 lanes of a beat address distinct banks except where lanes carry the
// SAME node (adjacent pins of one node on one net), so each bank serves at most one row per beat.
// The gather is therefore written bank-major: each of the 32 banks picks the row its lane asked
// for and does exactly one read, then each lane muxes its value from its bank. HLS sees one static
// read per bank per iteration -- no port conflict to schedule around. Meow.

#include "format/pin_record.hpp"
#include "modules/hpwl_gradient/slot_arrays.hpp"   // ROWS_PER_BANK

namespace plalgo {

constexpr int OFFSET_BITS_MAX  = 10;
constexpr int OFFSET_TABLE_MAX = 1 << OFFSET_BITS_MAX;           // measured need: <= 131 (44 designs)

// One offset table copy per lane: 16 random reads per cycle, one per copy. Meow.
static void load_offset_table(const float* offset_table_DDR, int offset_table_size,
                              float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX]) {
    // Copy the pin-offset table (pin position relative to its node) DDR -> BRAM, once per lane. Meow.
load_offsets:
    for (int i = 0; i < offset_table_size; i++) {
#pragma HLS PIPELINE II=1
        const float value = offset_table_DDR[i];
        for (int lane = 0; lane < pinrec::LANES; lane++) offset_BRAM[lane][i] = value;
    }
}

// This chunk's NUM_GROUP_ENDS entries: the beat at which each group ends in the stream (cumulative
// positions, not counts). Groups 0..14 are small-net degrees 2..16; groups 15..21 are large-net
// spans 2..8. Large nets are grouped by span, not degree: the packer may give a net more beats than
// ceil(degree/16), and the device never needs a large net's degree. Meow.
static void load_group_ends(const int32_t* group_ends_DDR, int group_end_REG[pinrec::NUM_GROUP_ENDS]) {
    // Copy this chunk's group boundaries DDR -> registers. Meow.
load_group_ends:
    for (int k = 0; k < pinrec::NUM_GROUP_ENDS; k++) {
#pragma HLS PIPELINE II=1
        group_end_REG[k] = group_ends_DDR[k];
    }
}

// Which group the beat sits in: the number of group ends at or before it. 22 parallel compares
// summed, no loop-carried state. Meow.
static int resolve_group(int beat, const int group_end_REG[pinrec::NUM_GROUP_ENDS]) {
#pragma HLS INLINE
    int group = 0;
    for (int k = 0; k < pinrec::NUM_GROUP_ENDS; k++) {
        if (beat >= group_end_REG[k]) group++;
    }
    return group;
}

// Window form: a large net's per-beat values sit in a shift register of the last MAX_SPAN beats
// (newest first), and its last beat reduces the newest `span` of them with a balanced tree. The
// shift has no logic between registers, so no arithmetic is loop-carried and the tree pipelines
// like the Dhar trees. Older entries take Op's identity (-inf for max, +inf for min, 0 for add);
// window[0] would do for max and min but double-count in a sum. Meow.
template <typename Op>
static float balanced_tree(const float window[pinrec::MAX_SPAN], int span) {
#pragma HLS INLINE
    static_assert(pinrec::MAX_SPAN == 8, "balanced_tree is a 3-level tree");
    float v[pinrec::MAX_SPAN];
#pragma HLS ARRAY_PARTITION variable=v complete dim=0
    for (int w = 0; w < pinrec::MAX_SPAN; w++) v[w] = w < span ? window[w] : Op::identity();
    const float v_0_1 = Op::apply(v[0], v[1]);
    const float v_2_3 = Op::apply(v[2], v[3]);
    const float v_4_5 = Op::apply(v[4], v[5]);
    const float v_6_7 = Op::apply(v[6], v[7]);

    const float v_0_3 = Op::apply(v_0_1, v_2_3);
    const float v_4_7 = Op::apply(v_4_5, v_6_7);
    return Op::apply(v_0_3, v_4_7);
}

struct DecodedLanes {
    uint32_t slot_idx[pinrec::LANES];
    uint32_t offset_idx[pinrec::LANES];
    bool     empty[pinrec::LANES];
};

static DecodedLanes decode_lanes(const pinrec::RecordBeat& rb, int offset_bits) {
#pragma HLS INLINE
    DecodedLanes d;
    for (int i = 0; i < pinrec::LANES; i++) {
        d.empty[i]      = rb.r[i] == pinrec::EMPTY_RECORD;
        d.slot_idx[i]       = pinrec::record_node_slot(rb.r[i], offset_bits);
        d.offset_idx[i] = d.empty[i] ? 0u : pinrec::record_offset_idx(rb.r[i], offset_bits);
    }
    return d;
}

// Bank-major gather: bank b reads the row of the (at most one distinct) node addressing it, then
// each lane takes its bank's value. Lanes of one repeated node share the read.
// The lane readout is a masked loop over the banks, NOT `bank_pos[slot % BANKS]`: with the dynamic
// index, the HLS C front end ran for 10+ minutes without finishing (bisected 2026-09-22); the masked
// form compiles in seconds and schedules beat_loop at II=1, depth 21. Same 32:1 mux either way. Meow.
static void gather_pin_positions(const DecodedLanes& d, const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],
                                 const float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX],
                                 float pin_pos[pinrec::LANES]) {
#pragma HLS INLINE
    float bank_pos[pinrec::BANKS];
#pragma HLS ARRAY_PARTITION variable=bank_pos complete dim=0
// 5 LSBs of slot_idx are used to select the bank (2^5 = 32 banks), remaining MSBs selects the row within URAM.
// Fully unrolled, synthesizes to 32 mux 16:1 for the URAM bank's read
    for (int b = 0; b < pinrec::BANKS; b++) {
        uint32_t row = 0;
        for (int i = 0; i < pinrec::LANES; i++)
            if (!d.empty[i] && d.slot_idx[i] % pinrec::BANKS == (uint32_t)b)
                row = d.slot_idx[i] / pinrec::BANKS;
        bank_pos[b] = pos_URAM[b][row];
    }
// Fully unrolled, synthesizes to 16 mux 32:1 for the URAM bank's write
    for (int i = 0; i < pinrec::LANES; i++) {
        float value = 0.0f;
        for (int b = 0; b < pinrec::BANKS; b++)
            if (d.slot_idx[i] % pinrec::BANKS == (uint32_t)b)
                value = bank_pos[b];
        pin_pos[i] = value + offset_BRAM[i][d.offset_idx[i]];
    }
}

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_RECORD_GATHER_HPP
