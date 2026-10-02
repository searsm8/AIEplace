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
// Large nets (17..96 pins, pin_record.hpp) follow the degree-16 group: one net per beat over
// span consecutive beats. Each beat's degree-16 tree gives that beat's max/min; a window of the
// last MAX_SPAN beats' values is reduced on the net's last beat, and the net's HPWL lands in lane 0
// of that beat.
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

// Find the current degree by referencing the beat_count table.
static int resolve_degree(int beat, const int beat_count_REG[pinrec::NET_DEGREES_PROCESSED]) {
#pragma HLS INLINE
    int degree = pinrec::MIN_NET_DEGREE;
    for (int k = 0; k < pinrec::NET_DEGREES_PROCESSED; k++) {
        if (beat >= beat_count_REG[k]) degree++;
    }
    return degree;
}

// FOR LARGE NETS ONLY: Find the current net's span by referencing the span_count table.
static int resolve_span(int beat, const int span_count_REG[pinrec::SPAN_GROUPS]) {
#pragma HLS INLINE
    int span = 2;
    for (int k = 0; k < pinrec::SPAN_GROUPS; k++) {
        if (beat >= span_count_REG[k]) span++;
    }
    return span;
}

// Window form: a large net's per-beat values sit in a shift register of the last MAX_SPAN beats
// (newest first), and its last beat reduces the newest `span` of them with a balanced tree. The
// shift has no logic between registers, so no arithmetic is loop-carried and the tree pipelines
// like the Dhar trees. Older entries take window[0]'s value -- neutral for max and min. Meow.
template <typename Op>
static float reduce_window(const float window[pinrec::MAX_SPAN], int span) {
#pragma HLS INLINE
    static_assert(pinrec::MAX_SPAN == 8, "reduce_window is a 3-level tree");
    float v[pinrec::MAX_SPAN];
#pragma HLS ARRAY_PARTITION variable=v complete dim=0
    for (int w = 0; w < pinrec::MAX_SPAN; w++) v[w] = w < span ? window[w] : window[0];
    const float v_0_1 = Op::apply(v[0], v[1]), v_2_3 = Op::apply(v[2], v[3]);
    const float v_4_5 = Op::apply(v[4], v[5]), v_6_7 = Op::apply(v[6], v[7]);
    return Op::apply(Op::apply(v_0_1, v_2_3), Op::apply(v_4_5, v_6_7));
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
#pragma HLS DEPENDENCE variable=pos_URAM inter false // tells HLS the read and write are disjoint, so it can schedule II=1
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

// The record stream -> per-net HPWL, against positions already resident in pos_URAM. Meow.
static void hpwl_beat_loop(const pinrec::RecordBeat* records_DDR, int num_beats,
                           const int beat_count_REG[pinrec::NET_DEGREES_PROCESSED], // cumulative table for how many beats at each degree
                           const int span_count_REG[pinrec::SPAN_GROUPS],           // cumulative table for how many beats in each large-net span
                           const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],      
                           const float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX],
                           OutBeat* out_beats_DDR, int offset_bits) {
    float hi_window[pinrec::MAX_SPAN] = {}, lo_window[pinrec::MAX_SPAN] = {};   // newest first
#pragma HLS ARRAY_PARTITION variable=hi_window complete dim=0
#pragma HLS ARRAY_PARTITION variable=lo_window complete dim=0
    int beat_in_net = 0;
beat_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
        // Large nets (17..96 pins) begin after beat_count[14] beats have been processed. 
        // A single large net is spread over *span* number of beats, read in at the same cadence as small nets.
        // Since each beat can hold 16 pins, a X-pin large net is processed over a span of ceil(X/16) beats (padding sometimes to avoid URAM bank conflicts)
        const bool large      = beat >= beat_count_REG[pinrec::NET_DEGREES_PROCESSED - 1]; // control signal for the large-net path
        const int  degree     = large ? pinrec::LANES : resolve_degree(beat, beat_count_REG);
        const int  span       = resolve_span(beat, span_count_REG);
        const bool last_beat  = beat_in_net == span - 1; // true when processing the last beat of a large net.

        const DecodedLanes d = decode_lanes(records_DDR[beat], offset_bits);
        // A pad (packer rule L7): an all-EMPTY large-net beat, invisible to the window and the counter.
        const bool pad = large && d.empty[0];

        float pin_pos[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=pin_pos complete dim=0
        gather_pin_positions(d, pos_URAM, offset_BRAM, pin_pos);

        // A large-net beat's EMPTY lanes trail its pins; lane 0's pin stands in, neutral to max and min.
        for (int i = 1; i < pinrec::LANES; i++)
            if (large && d.empty[i]) pin_pos[i] = pin_pos[0];

        TreeOutputs t = build_tree_outputs(pin_pos);
#pragma HLS ARRAY_PARTITION variable=t.max_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=t.min_deg complete dim=0

        const int degree_idx    = degree - pinrec::MIN_NET_DEGREE;
        const int nets_per_beat = pinrec::LANES / degree;

        // FOR LARGE NETS ONLY: hi and lo values each beat are held in a shift register
        if (!pad) {
            for (int w = pinrec::MAX_SPAN - 1; w > 0; w--) {
                hi_window[w] = hi_window[w - 1];
                lo_window[w] = lo_window[w - 1];
            }
            hi_window[0] = t.max_deg[degree_idx][0];
            lo_window[0] = t.min_deg[degree_idx][0];
        }

        // current hi and low found for the net: output of reduction tree.
        const float net_hi = reduce_window<MaxOp>(hi_window, span);
        const float net_lo = reduce_window<MinOp>(lo_window, span);
        if (large && !pad) beat_in_net = last_beat ? 0 : beat_in_net + 1;

        OutBeat out_beat;
#pragma HLS ARRAY_PARTITION variable=out_beat.v complete dim=0
    net_hpwl_write:   // a small net position is live iff its first lane holds a pin
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            const bool live = k < nets_per_beat && !d.empty[k * degree];
            if (large) out_beat.v[k] = k == 0 && last_beat && !pad ? net_hi - net_lo : 0.0f; // on the last beat, the hi and lo have finalized
            else       out_beat.v[k] = live ? t.max_deg[degree_idx][k] - t.min_deg[degree_idx][k] : 0.0f; // for small nets, each beat has full nets
        }
        out_beats_DDR[beat] = out_beat; // Output write to DDR
    }
}

static void hpwl_computer_v2(
        const pinrec::RecordBeat*  records_DDR,       // [num_beats] static stream, this axis
        int                        num_beats,
        const int*                 beat_count_DDR,    // [NET_DEGREES_PROCESSED] cumulative beats per degree
        const int*                 span_count_DDR,    // [SPAN_GROUPS] cumulative beats per large-net span
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
    int span_count_REG[pinrec::SPAN_GROUPS];
#pragma HLS ARRAY_PARTITION variable=span_count_REG complete dim=0
cache_spans:
    for (int k = 0; k < pinrec::SPAN_GROUPS; k++) span_count_REG[k] = span_count_DDR[k];

    load_slot_array(pos_DDR, num_slot_beats, pos_URAM);
    refresh_macro_pins(macro_pins_DDR, num_macro_pins, pos_URAM);
    load_offset_table(offset_table_DDR, offset_table_size, offset_BRAM);
    hpwl_beat_loop(records_DDR, num_beats, beat_count_REG, span_count_REG, pos_URAM, offset_BRAM, out_beats_DDR, offset_bits);
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_COMPUTER_V2_HPP
