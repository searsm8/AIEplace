#ifndef PL_ALGO_WA_SUMS_HPP
#define PL_ALGO_WA_SUMS_HPP

// wa_sums -- stage B of the WA-gradient beat loop (#41): pin positions + net bbox -> per-pin exp
// terms (Dhar Fig. 5) and per-net WA sums B+, C+, B-, C- (four dhar_tree<AddOp>, Fig. 6/7).
// Exponents are shifted by the net's bbox, so every a is in [0, 1]; exp comes from the host LUT
// exp(-d/gamma) with linear interpolation -- same arithmetic as sw_only computeHpwlPartials_CPU.
//
// Large nets: the net's bbox arrives from stage A on net_bboxes, popped on the net's first real
// beat (a blocking read: the wait for A to finish the net). Its sums cover all its beats, so each
// beat's degree-16 sums go into a window of the last MAX_SPAN beats, as A's bbox does; the net's
// last real beat reduces the newest `span` entries and pushes them to stage C on net_sums.
//
// Owns lut_BRAM inside the beat-loop DATAFLOW region (read only). Meow.

#include "modules/pin_bbox.hpp"

namespace plalgo {

constexpr int GRAD_LUT_MAX = 1024;   // production LUT is 242 entries (GAMMA_MULT / PLACE_STEP_NORM + 2)
constexpr int EXP_LOOKUPS  = 2 * pinrec::LANES;   // a+ and a- per lane

// (lut[i], lut[i+1]) in one word: an interpolated lookup is ONE read (D8). Meow.
struct LutPair { float lo; float hi; };

struct AddOp {
    static inline float apply(float a, float b) { return a + b; }
    static inline float identity() { return 0.0f; }
};

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
    BeatFlags flags;
};

struct NetSum { float Bp, Cp, Bm, Cm; };   // one large net's sums over all its beats

static void wa_sums(hls::stream<PinBeat>& pin_beats, hls::stream<NetBbox>& net_bboxes, int num_beats,
                    const LutPair lut_BRAM[EXP_LOOKUPS][GRAD_LUT_MAX], int lut_size, float inv_lut_step,
                    hls::stream<SumBeat>& sum_beats, hls::stream<NetSum>& net_sums) {
    float Bp_window[pinrec::MAX_SPAN] = {}, Cp_window[pinrec::MAX_SPAN] = {};   // newest first
    float Bm_window[pinrec::MAX_SPAN] = {}, Cm_window[pinrec::MAX_SPAN] = {};
#pragma HLS ARRAY_PARTITION variable=Bp_window complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cp_window complete dim=0
#pragma HLS ARRAY_PARTITION variable=Bm_window complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cm_window complete dim=0
    NetBbox large_bbox = {0.0f, 0.0f};   // the current large net's: only assigned, never computed on
wa_sums_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
        const PinBeat pb = pin_beats.read();
        const BeatFlags f = pb.flags;
        const int degree_idx = pb.degree - pinrec::MIN_NET_DEGREE;
        if (f.first) large_bbox = net_bboxes.read();

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
            const float net_max = f.large ? large_bbox.hi : pb.net_max[i];
            const float net_min = f.large ? large_bbox.lo : pb.net_min[i];
            sb.a_plus[i]  = pb.empty[i] ? 0.0f : lut_exp_pair(lut_BRAM[2 * i],     lut_size, inv_lut_step, net_max - x);
            sb.a_minus[i] = pb.empty[i] ? 0.0f : lut_exp_pair(lut_BRAM[2 * i + 1], lut_size, inv_lut_step, x - net_min);
            Bp_terms[i] = sb.a_plus[i];  Cp_terms[i] = sb.a_plus[i] * x;
            Bm_terms[i] = sb.a_minus[i]; Cm_terms[i] = sb.a_minus[i] * x;
            sb.x[i]        = x;
            sb.slot_idx[i] = pb.slot_idx[i];
            sb.empty[i]    = pb.empty[i];
        }
        sb.degree = pb.degree;
        sb.flags  = f;

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

        // ---- large nets: each beat's whole-beat sums (degree 16) into the window, reduced on the last ----
        if (!f.pad) {
            for (int w = pinrec::MAX_SPAN - 1; w > 0; w--) {
                Bp_window[w] = Bp_window[w - 1]; Cp_window[w] = Cp_window[w - 1];
                Bm_window[w] = Bm_window[w - 1]; Cm_window[w] = Cm_window[w - 1];
            }
            Bp_window[0] = Bp_deg[degree_idx][0]; Cp_window[0] = Cp_deg[degree_idx][0];
            Bm_window[0] = Bm_deg[degree_idx][0]; Cm_window[0] = Cm_deg[degree_idx][0];
        }
        const NetSum net_sum = {reduce_window<AddOp>(Bp_window, f.span), reduce_window<AddOp>(Cp_window, f.span),
                                reduce_window<AddOp>(Bm_window, f.span), reduce_window<AddOp>(Cm_window, f.span)};

        sum_beats.write(sb);
        if (f.last) net_sums.write(net_sum);
    }
}

} // namespace plalgo

#endif // PL_ALGO_WA_SUMS_HPP
