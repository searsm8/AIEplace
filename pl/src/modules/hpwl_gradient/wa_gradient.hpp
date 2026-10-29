#ifndef PL_HPWL_GRADIENT_WA_GRADIENT_HPP
#define PL_HPWL_GRADIENT_WA_GRADIENT_HPP

// wa_gradient -- stage C of the WA-gradient beat loop (#41): per-pin WA partial (combiner, Dhar
// Fig. 8, eq. 4) -> merge the lanes of a repeated node -> scatter-add into grad_URAM.
//
// The scatter-add is bank-major like the gather: each bank finds the one run head addressing it
// and does one read-add-write. The host keeps a node's updates HAZARD_DISTANCE beats apart (packer
// rule B5); that distance is declared to HLS as a TRUE dependence, so HLS must fit the RMW round
// trip inside it (or raise II) -- the schedule and the pragma are one contract.
//
// Owns grad_URAM inside the beat-loop DATAFLOW region. Meow.
// From vck5000/bring_up/hpwl_gradient_computer/src/modules/wa_gradient.hpp @ 41f2193 (#45). Meow.

#include "modules/hpwl_gradient/wa_sums.hpp"

namespace plalgo {

// wa_gradient's adds stay at IP latency 1, not beat_add's 2: the grad_URAM read-add-write must fit
// HAZARD_DISTANCE (latency 2 gives II=2), and HLS 2022.2 builds every fadd in one loop as a single
// module, so beat_add here came out at latency 1 anyway (build H, 2026-10-28). Binding them also
// keeps HLS from fusing a beat_mul into an fmadd. Meow.
static inline float gradient_add(float a, float b) {
#pragma HLS INLINE
    const float sum = a + b;
#pragma HLS BIND_OP variable=sum op=fadd impl=primitivedsp latency=1
    return sum;
}

// A large net's beats take the net's sums from stage B (net_sums, popped on its first real beat;
// blocking, like B's bbox pop) in net position 0 -- every lane of a degree-16 beat is net 0. Meow.
static void wa_gradient(hls::stream<SumBeat>& sum_beats, hls::stream<NetSum>& net_sums, int num_beats, float inv_gamma,
                        float grad_URAM[pinrec::BANKS][ROWS_PER_BANK], int first_fixed_slot) {
    NetSum large_sum = {0.0f, 0.0f, 0.0f, 0.0f};   // the current large net's: only assigned, never computed on
    // One beat of pins per cycle: turn each pin's net sums into its WA gradient, and add it into
    // the gradient of the node the pin belongs to. Meow.
wa_gradient_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM type=inter direction=RAW distance=pinrec::HAZARD_DISTANCE dependent=true
        const SumBeat sb = sum_beats.read();
        if (sb.flags.first) large_sum = net_sums.read();
        NetSums s = sb.s;
#pragma HLS ARRAY_PARTITION variable=s.Bp complete dim=0
#pragma HLS ARRAY_PARTITION variable=s.Cp complete dim=0
#pragma HLS ARRAY_PARTITION variable=s.Bm complete dim=0
#pragma HLS ARRAY_PARTITION variable=s.Cm complete dim=0
        if (sb.flags.large) { s.Bp[0] = large_sum.Bp; s.Cp[0] = large_sum.Cp; s.Bm[0] = large_sum.Bm; s.Cm[0] = large_sum.Cm; }

        // ---- one 1/B^2 per net (not per pin) ----
        float inv_Bp2[pinrec::MAX_NETS_PER_BEAT], inv_Bm2[pinrec::MAX_NETS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=inv_Bp2 complete dim=0
#pragma HLS ARRAY_PARTITION variable=inv_Bm2 complete dim=0
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            inv_Bp2[k] = 1.0f / beat_mul(s.Bp[k], s.Bp[k]);
            inv_Bm2[k] = 1.0f / beat_mul(s.Bm[k], s.Bm[k]);
        }

        // ---- combiners (Fig. 8, eq. 4): each pin's WA partial ----
        //   g = ((1 + x/γ)·B+ − C+/γ)·(a+/B+²) − ((1 − x/γ)·B− + C−/γ)·(a−/B−²)   Meow.
        float g[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=g complete dim=0
        for (int i = 0; i < pinrec::LANES; i++) {
            const int k = i / sb.degree;
            const float x_over_gamma = beat_mul(sb.x[i], inv_gamma);
            const float plus_term  = beat_mul(beat_sub(beat_mul(gradient_add(1.0f, x_over_gamma), s.Bp[k]), beat_mul(s.Cp[k], inv_gamma)),
                                              beat_mul(sb.a_plus[i], inv_Bp2[k]));
            const float minus_term = beat_mul(gradient_add(beat_mul(beat_sub(1.0f, x_over_gamma), s.Bm[k]), beat_mul(s.Cm[k], inv_gamma)),
                                              beat_mul(sb.a_minus[i], inv_Bm2[k]));
            g[i] = beat_sub(plus_term, minus_term);
        }

        // ---- merge: a node's repeated pins sit in adjacent lanes with the same slot (packer rule
        //      B3); a segmented suffix scan (Hillis-Steele, 4 steps) leaves each run's total in its
        //      head lane ----
        bool same_as_next[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=same_as_next complete dim=0
        for (int i = 0; i < pinrec::LANES; i++)
            same_as_next[i] = i + 1 < pinrec::LANES && !sb.empty[i] && sb.slot_idx[i] == sb.slot_idx[i + 1];
        for (int step = 1; step < pinrec::LANES; step *= 2) {
            float next_g[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=next_g complete dim=0
            for (int i = 0; i < pinrec::LANES; i++) {
                const bool joined = i + step < pinrec::LANES && !sb.empty[i] && sb.slot_idx[i] == sb.slot_idx[i + step];
                next_g[i] = joined ? gradient_add(g[i], g[i + step]) : g[i];
            }
            for (int i = 0; i < pinrec::LANES; i++) g[i] = next_g[i];
        }

        // ---- scatter-add, bank-major: bank b takes the one movable run head addressing it ----
        for (int b = 0; b < pinrec::BANKS; b++) {
            bool     hit = false;
            uint32_t row = 0;
            float    add = 0.0f;
            for (int i = 0; i < pinrec::LANES; i++) {
                const bool head = i == 0 || !same_as_next[i - 1];
                if (head && !sb.empty[i] && sb.slot_idx[i] < (uint32_t)first_fixed_slot && sb.slot_idx[i] % pinrec::BANKS == (uint32_t)b) {
                    hit = true; row = sb.slot_idx[i] / pinrec::BANKS; add = g[i];
                }
            }
            if (hit) grad_URAM[b][row] = gradient_add(grad_URAM[b][row], add);
        }
    }
}

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_WA_GRADIENT_HPP
