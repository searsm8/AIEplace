#ifndef PL_ALGO_HPWL_COMPUTER_V2_HPP
#define PL_ALGO_HPWL_COMPUTER_V2_HPP

// hpwl_computer_v2 -- per-net HPWL from the static pin-record stream (#41), one axis per call.
//
// v1 (hpwl_computer) read 16 pre-gathered absolute pin positions per beat from DDR, which meant
// some earlier pass had already done the random node->pin gather at DDR speed. v2 moves that
// gather on chip: node positions sit in banked URAM, and the DDR stream carries only static
// 32-bit records (pin_record.hpp). All random access is on chip; every DDR access is sequential.
//
//   load_pos        slot-major positions DDR -> pos_URAM, 16 slots per beat (one 512-bit beat)
//   refresh_macros  pos[macro pin slot] = pos[macro slot] + offset, from a sequential list
//   beat_loop       record beat -> gather 16 positions + offsets -> v1's Dhar trees -> per-net HPWL
//
// The host guarantees the 16 lanes of a beat address distinct banks except where lanes carry the
// SAME node (adjacent pins of one node on one net), so each bank serves at most one row per beat.
// The gather is therefore written bank-major: each of the 32 banks picks the row its lane asked
// for and does exactly one read, then each lane muxes its value from its bank. HLS sees one static
// read per bank per iteration -- no port conflict to schedule around. Meow.

#include "pin_record.hpp"
#include "modules/hpwl_computer.hpp"   // dhar_tree, MaxOp/MinOp, TreeOutputs, OutBeat (v1)

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
constexpr int OFFSET_BITS_MAX  = 10;
constexpr int OFFSET_TABLE_MAX = 1 << OFFSET_BITS_MAX;           // measured need: <= 131 (44 designs)

static int resolve_degree(int beat, const int beat_count_REG[pinrec::NET_DEGREES_PROCESSED]) {
#pragma HLS INLINE
    int degree = pinrec::MIN_NET_DEGREE;
    for (int k = 0; k < pinrec::NET_DEGREES_PROCESSED; k++) {
        if (beat >= beat_count_REG[k]) degree++;
    }
    return degree;
}

// Slot-major DDR beat b holds slots 16b..16b+15 -> banks (b&1)*16 + j, row b>>1: sixteen distinct
// banks, so a whole beat lands in one cycle. Shared by every array loaded or drained this way. Meow.
static void load_slot_array(const pinrec::SlotBeat* src_DDR, int num_slot_beats,
                            float dst_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
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

static void refresh_macro_pins(const pinrec::MacroPinRef* macro_pins_DDR, int num_macro_pins,
                               float pos_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
refresh_macros:   // reads macro slots, writes macro-pin slots: disjoint, so no carried dependence
    for (int e = 0; e < num_macro_pins; e++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=pos_URAM inter false
        const pinrec::MacroPinRef ref = macro_pins_DDR[e];
        if (ref.pin_slot == pinrec::MACRO_PIN_SKIP) continue;   // fold-schedule padding
        const float macro_pos = pos_URAM[ref.macro_slot % pinrec::BANKS][ref.macro_slot / pinrec::BANKS];
        pos_URAM[ref.pin_slot % pinrec::BANKS][ref.pin_slot / pinrec::BANKS] = macro_pos + ref.offset;
    }
}

// One offset table copy per lane: 16 random reads per cycle, one per copy. Meow.
static void load_offset_table(const float* offset_table_DDR, int offset_table_size,
                              float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX]) {
load_offsets:
    for (int i = 0; i < offset_table_size; i++) {
#pragma HLS PIPELINE II=1
        const float value = offset_table_DDR[i];
        for (int lane = 0; lane < pinrec::LANES; lane++) offset_BRAM[lane][i] = value;
    }
}

struct DecodedLanes {
    uint32_t slot[pinrec::LANES];
    uint32_t offset_idx[pinrec::LANES];
    bool     empty[pinrec::LANES];
};

static DecodedLanes decode_lanes(const pinrec::RecordBeat& rb, int offset_bits) {
#pragma HLS INLINE
    DecodedLanes d;
    for (int i = 0; i < pinrec::LANES; i++) {
        d.empty[i]      = rb.r[i] == pinrec::EMPTY_RECORD;
        d.slot[i]       = pinrec::record_node_slot(rb.r[i], offset_bits);
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
    for (int b = 0; b < pinrec::BANKS; b++) {
        uint32_t row = 0;
        for (int i = 0; i < pinrec::LANES; i++)
            if (!d.empty[i] && d.slot[i] % pinrec::BANKS == (uint32_t)b) row = d.slot[i] / pinrec::BANKS;
        bank_pos[b] = pos_URAM[b][row];
    }
    for (int i = 0; i < pinrec::LANES; i++) {
        float value = 0.0f;
        for (int b = 0; b < pinrec::BANKS; b++)
            if (d.slot[i] % pinrec::BANKS == (uint32_t)b) value = bank_pos[b];
        pin_pos[i] = value + offset_BRAM[i][d.offset_idx[i]];
    }
}

// The record stream -> per-net HPWL, against positions already resident in pos_URAM. Meow.
static void hpwl_beat_loop(const pinrec::RecordBeat* records_DDR, int num_beats,
                           const int beat_count_REG[pinrec::NET_DEGREES_PROCESSED],
                           const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],
                           const float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX],
                           OutBeat* out_beats_DDR, int offset_bits) {
beat_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
        const int degree = resolve_degree(beat, beat_count_REG);
        const DecodedLanes d = decode_lanes(records_DDR[beat], offset_bits);

        float pin_pos[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=pin_pos complete dim=0
        gather_pin_positions(d, pos_URAM, offset_BRAM, pin_pos);

        TreeOutputs t = build_tree_outputs(pin_pos);
#pragma HLS ARRAY_PARTITION variable=t.max_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=t.min_deg complete dim=0

        const int degree_idx    = degree - pinrec::MIN_NET_DEGREE;
        const int nets_per_beat = pinrec::LANES / degree;
        OutBeat out_beat;
#pragma HLS ARRAY_PARTITION variable=out_beat.v complete dim=0
    net_hpwl_write:   // a net position is live iff its first lane holds a pin
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            const bool live = k < nets_per_beat && !d.empty[k * degree];
            out_beat.v[k] = live ? t.max_deg[degree_idx][k] - t.min_deg[degree_idx][k] : 0.0f;
        }
        out_beats_DDR[beat] = out_beat;
    }
}

static void hpwl_computer_v2(
        const pinrec::RecordBeat*  records_DDR,       // [num_beats] static stream, this axis
        int                        num_beats,
        const int*                 beat_count_DDR,    // [NET_DEGREES_PROCESSED] cumulative beats per degree
        const pinrec::SlotBeat*    pos_DDR,           // [num_slot_beats] slot-major positions, this axis
        int                        num_slot_beats,
        const pinrec::MacroPinRef* macro_pins_DDR,    // [num_macro_pins] refresh list, this axis
        int                        num_macro_pins,
        const float*               offset_table_DDR,  // [offset_table_size] this axis
        int                        offset_table_size,
        OutBeat*                   out_beats_DDR,     // [num_beats] per-net HPWL, 0 for EMPTY nets
        int                        offset_bits) {

    ONCHIP_ARRAY float pos_URAM[pinrec::BANKS][ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=pos_URAM complete dim=1   // 32 independent banks
#pragma HLS BIND_STORAGE variable=pos_URAM type=ram_2p impl=uram
    ONCHIP_ARRAY float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX];
#pragma HLS ARRAY_PARTITION variable=offset_BRAM complete dim=1

    int beat_count_REG[pinrec::NET_DEGREES_PROCESSED];
#pragma HLS ARRAY_PARTITION variable=beat_count_REG complete dim=0
cache_counts:
    for (int k = 0; k < pinrec::NET_DEGREES_PROCESSED; k++) beat_count_REG[k] = beat_count_DDR[k];

    load_slot_array(pos_DDR, num_slot_beats, pos_URAM);
    refresh_macro_pins(macro_pins_DDR, num_macro_pins, pos_URAM);
    load_offset_table(offset_table_DDR, offset_table_size, offset_BRAM);
    hpwl_beat_loop(records_DDR, num_beats, beat_count_REG, pos_URAM, offset_BRAM, out_beats_DDR, offset_bits);
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_COMPUTER_V2_HPP
