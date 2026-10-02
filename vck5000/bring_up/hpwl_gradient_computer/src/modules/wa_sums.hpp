#ifndef PL_ALGO_WA_SUMS_HPP
#define PL_ALGO_WA_SUMS_HPP

// wa_sums -- stage B of the WA-gradient beat loop (#41): pin positions + net bbox -> per-pin exp
// terms (Dhar Fig. 5) and per-net WA sums B+, C+, B-, C- (four dhar_tree<AddOp>, Fig. 6/7).
// Exponents are shifted by the net's bbox, so every a is in [0, 1]; exp comes from the host LUT
// exp(-d/gamma) with linear interpolation -- same arithmetic as sw_only computeHpwlPartials_CPU.
//
// Owns lut_BRAM inside the beat-loop DATAFLOW region (read only). Meow.

#include "modules/pin_bbox.hpp"

namespace plalgo {

constexpr int GRAD_LUT_MAX = 1024;   // production LUT is 242 entries (GAMMA_MULT / PLACE_STEP_NORM + 2)
constexpr int EXP_LOOKUPS  = 2 * pinrec::LANES;   // a+ and a- per lane

// (lut[i], lut[i+1]) in one word: an interpolated lookup is ONE read (D8). Meow.
struct LutPair { float lo; float hi; };

struct AddOp { static inline float apply(float a, float b) { return a + b; } };

// Same arithmetic as hpwl_lut_exp, so the result is bit-identical to the pl_algo module's. Meow.
static inline float lut_exp_pair(const LutPair lut_BRAM[GRAD_LUT_MAX], int lut_size, float inv_lut_step, float d) {
#pragma HLS INLINE
    const float idx_f = d * inv_lut_step;
    const int   idx   = (int)idx_f;
    if (idx >= lut_size - 1) return 0.0f;
    const float frac  = idx_f - (float)idx;
    const LutPair pair = lut_BRAM[idx];
    return pair.lo * (1.0f - frac) + pair.hi * frac;
}

struct NetSums {
    float Bp[pinrec::MAX_NETS_PER_BEAT], Cp[pinrec::MAX_NETS_PER_BEAT];
    float Bm[pinrec::MAX_NETS_PER_BEAT], Cm[pinrec::MAX_NETS_PER_BEAT];
};

// What one beat leaves stage B with: what the combiner needs per pin, plus the per-net sums. Meow.
struct SumBeat {
    float    x[pinrec::LANES];
    float    a_plus[pinrec::LANES];
    float    a_minus[pinrec::LANES];
    NetSums  s;
    uint32_t slot_idx[pinrec::LANES];
    bool     empty[pinrec::LANES];
    int      degree;
};

static void wa_sums(hls::stream<PinBeat>& pin_beats, int num_beats,
                    const LutPair lut_BRAM[EXP_LOOKUPS][GRAD_LUT_MAX], int lut_size, float inv_lut_step,
                    hls::stream<SumBeat>& sum_beats) {
wa_sums_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
        const PinBeat pb = pin_beats.read();
        const int degree_idx = pb.degree - pinrec::MIN_NET_DEGREE;

        // ---- term generators (Fig. 5) ----
        SumBeat sb;
#pragma HLS ARRAY_PARTITION variable=sb.x complete dim=0
#pragma HLS ARRAY_PARTITION variable=sb.a_plus complete dim=0
#pragma HLS ARRAY_PARTITION variable=sb.a_minus complete dim=0
#pragma HLS ARRAY_PARTITION variable=sb.s.Bp complete dim=0
#pragma HLS ARRAY_PARTITION variable=sb.s.Cp complete dim=0
#pragma HLS ARRAY_PARTITION variable=sb.s.Bm complete dim=0
#pragma HLS ARRAY_PARTITION variable=sb.s.Cm complete dim=0
#pragma HLS ARRAY_PARTITION variable=sb.slot_idx complete dim=0
#pragma HLS ARRAY_PARTITION variable=sb.empty complete dim=0
        float Bp_terms[pinrec::LANES], Cp_terms[pinrec::LANES], Bm_terms[pinrec::LANES], Cm_terms[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=Bp_terms complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cp_terms complete dim=0
#pragma HLS ARRAY_PARTITION variable=Bm_terms complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cm_terms complete dim=0
        for (int i = 0; i < pinrec::LANES; i++) {
            const float x = pb.x[i];
            // Each lane uses two lut_BRAM copies, one for a+ and one for a-, so the two reads can happen in one cycle.
            // Therefore, 32 lut_BRAM copies required to service 16 lanes at once.
            sb.a_plus[i]  = pb.empty[i] ? 0.0f : lut_exp_pair(lut_BRAM[2 * i],     lut_size, inv_lut_step, pb.net_max[i] - x);
            sb.a_minus[i] = pb.empty[i] ? 0.0f : lut_exp_pair(lut_BRAM[2 * i + 1], lut_size, inv_lut_step, x - pb.net_min[i]);
            Bp_terms[i] = sb.a_plus[i];  Cp_terms[i] = sb.a_plus[i] * x;
            Bm_terms[i] = sb.a_minus[i]; Cm_terms[i] = sb.a_minus[i] * x;
            sb.x[i]        = x;
            sb.slot_idx[i] = pb.slot_idx[i];
            sb.empty[i]    = pb.empty[i];
        }
        sb.degree = pb.degree;

        // ---- four sum trees (Fig. 6/7): every contiguous-segment sum for every degree ----
        float Bp_deg[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
        float Cp_deg[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
        float Bm_deg[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
        float Cm_deg[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=Bp_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cp_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=Bm_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cm_deg complete dim=0
        dhar_tree<AddOp>(Bp_terms, Bp_deg);
        dhar_tree<AddOp>(Cp_terms, Cp_deg);
        dhar_tree<AddOp>(Bm_terms, Bm_deg);
        dhar_tree<AddOp>(Cm_terms, Cm_deg);

        // ---- per net position: this degree's sums ----
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            sb.s.Bp[k] = Bp_deg[degree_idx][k]; sb.s.Cp[k] = Cp_deg[degree_idx][k];
            sb.s.Bm[k] = Bm_deg[degree_idx][k]; sb.s.Cm[k] = Cm_deg[degree_idx][k];
        }
        sum_beats.write(sb);
    }
}

} // namespace plalgo

#endif // PL_ALGO_WA_SUMS_HPP
