#ifndef PL_ALGO_HPWL_COMPUTER_HPP
#define PL_ALGO_HPWL_COMPUTER_HPP

#ifndef PL_TIER1_STUB
#include <hls_stream.h>
#endif

namespace plalgo {

constexpr int LANES              = 16;
constexpr int MIN_NET_DEGREE     = 2;
constexpr int NET_DEGREES_PROCESSED = LANES - MIN_NET_DEGREE + 1;

struct InBeat {
    float v[LANES];
};

struct BeatInfo {
    int degree;
    int nets_per_beat;
    int net_offset;
    int real_net_count;
};

static BeatInfo resolve_beat(int beat, const int net_count_BRAM[NET_DEGREES_PROCESSED], const int beat_count_BRAM[NET_DEGREES_PROCESSED]) {
    int degree = MIN_NET_DEGREE;
    for (int k = 0; k < NET_DEGREES_PROCESSED; k++) { // what does this synthesize to?
        if (beat >= beat_count_BRAM[k]) degree++;
    }

    int degree_idx         = degree - MIN_NET_DEGREE;
    int group_start_nets   = (degree_idx > 0) ? net_count_BRAM[degree_idx-1]  : 0;
    int group_start_beats  = (degree_idx > 0) ? beat_count_BRAM[degree_idx-1] : 0;
    int per_degree_nets    = net_count_BRAM[degree_idx] - group_start_nets;

    BeatInfo info;
    info.degree        = degree;
    info.nets_per_beat = LANES / degree;
    int nets_before     = (beat - group_start_beats) * info.nets_per_beat;
    int remaining       = per_degree_nets - nets_before;
    info.net_offset     = group_start_nets + nets_before;
    info.real_net_count = (info.nets_per_beat < remaining) ? info.nets_per_beat : remaining;
    return info;
}

constexpr int MAX_NETS_PER_BEAT = LANES / MIN_NET_DEGREE; // 16/2 = 8

struct OutBeat {
    float v[MAX_NETS_PER_BEAT];
};

struct TreeOutputs {
    float max_deg[NET_DEGREES_PROCESSED][MAX_NETS_PER_BEAT];
    float min_deg[NET_DEGREES_PROCESSED][MAX_NETS_PER_BEAT];
};

struct MaxOp { static inline float apply(float a, float b) { return a > b ? a : b; } };
struct MinOp { static inline float apply(float a, float b) { return a < b ? a : b; } };

// Dhar 2019's multi-output tree (Fig. 6/7), genericized over the combining operator: reused with
// MaxOp/MinOp here for bbox/HPWL, and with an AddOp for the term-sum trees in hpwl_gradient_computer.
template<typename Op>
static void dhar_tree(const float in[LANES], float out_deg[NET_DEGREES_PROCESSED][MAX_NETS_PER_BEAT]) {
    float v_0_1 = Op::apply(in[0], in[1]),     v_2_3 = Op::apply(in[2], in[3]);
    float v_4_5 = Op::apply(in[4], in[5]),     v_6_7 = Op::apply(in[6], in[7]);
    float v_8_9 = Op::apply(in[8], in[9]),     v_10_11 = Op::apply(in[10], in[11]);
    float v_12_13 = Op::apply(in[12], in[13]), v_14_15 = Op::apply(in[14], in[15]);

    float v_0_3 = Op::apply(v_0_1, v_2_3),   v_4_7  = Op::apply(v_4_5, v_6_7);
    float v_8_11 = Op::apply(v_8_9, v_10_11), v_12_15 = Op::apply(v_12_13, v_14_15);

    float v_0_7 = Op::apply(v_0_3, v_4_7), v_8_15 = Op::apply(v_8_11, v_12_15);
    float v_0_15 = Op::apply(v_0_7, v_8_15);

    float v_6_9   = Op::apply(v_6_7, v_8_9);
    float v_10_13 = Op::apply(v_10_11, v_12_13);
    float v_4_6   = Op::apply(v_4_5, in[6]);
    float v_8_13  = Op::apply(v_8_11, v_12_13);
    float v_8_10  = Op::apply(v_8_9, in[10]);
    float v_8_12  = Op::apply(v_8_11, in[12]);
    float v_12_14 = Op::apply(v_12_13, in[14]);
    float v_8_14  = Op::apply(v_8_11, v_12_14);

    // out_deg[idx] = the outputs for nets of degree (idx + 2)
    // degree 2 has 8 nets per beat, 8 outputs
    out_deg[0][0] = v_0_1; out_deg[0][1] = v_2_3;
    out_deg[0][2] = v_4_5; out_deg[0][3] = v_6_7;
    out_deg[0][4] = v_8_9; out_deg[0][5] = v_10_11;
    out_deg[0][6] = v_12_13; out_deg[0][7] = v_14_15;

    // degree 3 has 5 nets per beat, 5 outputs
    out_deg[1][0] = Op::apply(v_0_1, in[2]);
    out_deg[1][1] = Op::apply(in[3], v_4_5);
    out_deg[1][2] = Op::apply(v_6_7, in[8]);
    out_deg[1][3] = Op::apply(in[9], v_10_11);
    out_deg[1][4] = v_12_14;

    out_deg[2][0] = v_0_3;
    out_deg[2][1] = v_4_7;
    out_deg[2][2] = v_8_11;
    out_deg[2][3] = v_12_15;

    out_deg[3][0] = Op::apply(v_0_3, in[4]);
    out_deg[3][1] = Op::apply(in[5], v_6_9);
    out_deg[3][2] = Op::apply(v_10_13, in[14]);

    out_deg[4][0] = Op::apply(v_0_3, v_4_5);
    out_deg[4][1] = Op::apply(v_6_7, v_8_11);

    out_deg[5][0] = Op::apply(v_0_3, v_4_6);
    out_deg[5][1] = Op::apply(in[7], v_8_13);

    out_deg[6][0] = v_0_7;
    out_deg[6][1] = v_8_15;

    // degree 9 thru 16 have only 1 net per beat
    out_deg[7][0]  = Op::apply(v_0_7, in[8]);
    out_deg[8][0]  = Op::apply(v_0_7, v_8_9);
    out_deg[9][0]  = Op::apply(v_0_7, v_8_10);
    out_deg[10][0] = Op::apply(v_0_7, v_8_11);
    out_deg[11][0] = Op::apply(v_0_7, v_8_12);
    out_deg[12][0] = Op::apply(v_0_7, v_8_13);
    out_deg[13][0] = Op::apply(v_0_7, v_8_14);
    out_deg[14][0] = v_0_15;
}

static TreeOutputs build_tree_outputs(const float in[LANES]) {
    TreeOutputs t;
    dhar_tree<MaxOp>(in, t.max_deg);
    dhar_tree<MinOp>(in, t.min_deg);
    return t;
}

// Synthesize a mux for each output lane, selecting the correct MAX term based on degree.
static void select_lane_hpwl(const TreeOutputs& t, int degree, float lane_hpwl[LANES]) {
    int j = degree - MIN_NET_DEGREE;
    for (int i = 0; i < LANES; i++) {
        int segment_id = i / degree;
        lane_hpwl[i] = t.max_deg[j][segment_id] - t.min_deg[j][segment_id];
    }
}

static void hpwl_computer(
        const int*  net_count,
        const int*  beat_count,
        const InBeat* pin_beats,
        OutBeat*    out_beats,
        int         num_beats) {

    int net_count_BRAM[NET_DEGREES_PROCESSED];
    int beat_count_BRAM[NET_DEGREES_PROCESSED];
cache_counts:
    for (int i = 0; i < NET_DEGREES_PROCESSED; i++) {
        net_count_BRAM[i]  = net_count[i];
        beat_count_BRAM[i] = beat_count[i];
    }

beat_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
        BeatInfo    info = resolve_beat(beat, net_count_BRAM, beat_count_BRAM);
        TreeOutputs t    = build_tree_outputs(pin_beats[beat].v);

#pragma HLS ARRAY_PARTITION variable=t.max_deg complete dim=0 // Directs HLS to build these as registers, not BRAMs,
#pragma HLS ARRAY_PARTITION variable=t.min_deg complete dim=0 // which enables access to all lanes in one cycle 

        float lane_hpwl[LANES];
#pragma HLS ARRAY_PARTITION variable=lane_hpwl complete dim=0
        select_lane_hpwl(t, info.degree, lane_hpwl);

        OutBeat out_beat;
#pragma HLS ARRAY_PARTITION variable=out_beat.v complete dim=0
    net_hpwl_write: // pack results computed this cycle into OutBeat
        for (int net = 0; net < MAX_NETS_PER_BEAT; net++) {
            out_beat.v[net] = (net < info.real_net_count) ? lane_hpwl[net * info.degree] : 0.0f;
        }
        out_beats[beat] = out_beat; // write the OutBeat all at once to achieve II=1
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_COMPUTER_HPP
