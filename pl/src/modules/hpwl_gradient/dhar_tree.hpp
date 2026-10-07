#ifndef PL_HPWL_GRADIENT_DHAR_TREE_HPP
#define PL_HPWL_GRADIENT_DHAR_TREE_HPP

// dhar_tree -- Dhar 2019's multi-output tree (Fig. 6/7): every contiguous-segment reduction of a
// 16-lane beat, for every net degree 2..16 at once. Used with MaxOp/MinOp for each pin's net bbox
// (hpwl_CU) and with AddOp for the four WA term sums (wa_sums).
// From vck5000/bring_up/hpwl_computer/src/modules/hpwl_computer.hpp @ 41f2193 (#45). Meow.

#include "format/pin_record.hpp"

#include <limits>

namespace plalgo {

// The contract's constants, not a second definition of them (hpwl_computer v1 had its own). Meow.
using pinrec::LANES;
using pinrec::MIN_NET_DEGREE;
using pinrec::NET_DEGREES_PROCESSED;
using pinrec::MAX_NETS_PER_BEAT;

// identity(): the value that leaves apply() unchanged, for masked tree inputs. Meow.
struct MaxOp {
    static inline float apply(float a, float b) { return a > b ? a : b; }
    static inline float identity() { return -std::numeric_limits<float>::infinity(); }
};
struct MinOp {
    static inline float apply(float a, float b) { return a < b ? a : b; }
    static inline float identity() { return std::numeric_limits<float>::infinity(); }
};

// Genericized over the combining operator: MaxOp/MinOp for bbox/HPWL, AddOp for wa_sums' term sums.
template<typename Op>
static void dhar_tree(const float in[LANES], float out_deg[NET_DEGREES_PROCESSED][MAX_NETS_PER_BEAT]) {
    // 4-tier balanced tree, 16 inputs -> 8 outputs -> 4 outputs -> 2 outputs -> 1 output.
    float v_0_1   = Op::apply(in[0], in[1]),     v_2_3   = Op::apply(in[2], in[3]);
    float v_4_5   = Op::apply(in[4], in[5]),     v_6_7   = Op::apply(in[6], in[7]);
    float v_8_9   = Op::apply(in[8], in[9]),     v_10_11 = Op::apply(in[10], in[11]);
    float v_12_13 = Op::apply(in[12], in[13]),   v_14_15 = Op::apply(in[14], in[15]);

    float v_0_3  = Op::apply(v_0_1, v_2_3),   v_4_7   = Op::apply(v_4_5, v_6_7);
    float v_8_11 = Op::apply(v_8_9, v_10_11), v_12_15 = Op::apply(v_12_13, v_14_15);

    float v_0_7  = Op::apply(v_0_3, v_4_7),   v_8_15  = Op::apply(v_8_11, v_12_15);

    float v_0_15 = Op::apply(v_0_7, v_8_15);

    // Additional ops to form all remaining outputs.
    float v_6_9   = Op::apply(v_6_7, v_8_9);
    float v_10_13 = Op::apply(v_10_11, v_12_13);
    float v_4_6   = Op::apply(v_4_5, in[6]);
    float v_8_13  = Op::apply(v_8_11, v_12_13);
    float v_8_10  = Op::apply(v_8_9, in[10]);
    float v_8_12  = Op::apply(v_8_11, in[12]);
    float v_12_14 = Op::apply(v_12_13, in[14]);
    float v_8_14  = Op::apply(v_8_11, v_12_14);

    // out_deg[idx] = the outputs for nets of degree (idx + 2)
    // degree 2 (at index 0) has 8 nets per beat, 8 outputs
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

    // degree 4 has 4 nets per beat, 4 outputs
    out_deg[2][0] = v_0_3;
    out_deg[2][1] = v_4_7;
    out_deg[2][2] = v_8_11;
    out_deg[2][3] = v_12_15;

    // etc...
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

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_DHAR_TREE_HPP
