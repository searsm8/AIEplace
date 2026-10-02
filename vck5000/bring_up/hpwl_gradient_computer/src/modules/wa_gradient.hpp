#ifndef PL_ALGO_WA_GRADIENT_HPP
#define PL_ALGO_WA_GRADIENT_HPP

// wa_gradient -- stage C of the WA-gradient beat loop (#41): per-pin WA partial (combiner, Dhar
// Fig. 8, eq. 4) -> merge the lanes of a repeated node -> scatter-add into grad_URAM.
//
// The scatter-add is bank-major like the gather: each bank finds the one run head addressing it
// and does one read-add-write. The host keeps a node's updates HAZARD_DISTANCE beats apart (packer
// rule B5); that distance is declared to HLS as a TRUE dependence, so HLS must fit the RMW round
// trip inside it (or raise II) -- the schedule and the pragma are one contract.
//
// Owns grad_URAM inside the beat-loop DATAFLOW region. Meow.

#include "modules/wa_sums.hpp"

namespace plalgo {

static void wa_gradient(hls::stream<SumBeat>& sum_beats, int num_beats, float inv_gamma,
                        float grad_URAM[pinrec::BANKS][ROWS_PER_BANK], int first_fixed_slot) {
wa_gradient_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM type=inter direction=RAW distance=pinrec::HAZARD_DISTANCE dependent=true
        const SumBeat sb = sum_beats.read();

        // ---- one 1/B^2 per net (not per pin) ----
        float inv_Bp2[pinrec::MAX_NETS_PER_BEAT], inv_Bm2[pinrec::MAX_NETS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=inv_Bp2 complete dim=0
#pragma HLS ARRAY_PARTITION variable=inv_Bm2 complete dim=0
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            inv_Bp2[k] = 1.0f / (sb.s.Bp[k] * sb.s.Bp[k]);
            inv_Bm2[k] = 1.0f / (sb.s.Bm[k] * sb.s.Bm[k]);
        }

        // ---- combiners (Fig. 8, eq. 4): each pin's WA partial ----
        float g[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=g complete dim=0
        for (int i = 0; i < pinrec::LANES; i++) {
            const int k = i / sb.degree;
            g[i] = ((1.0f + sb.x[i] * inv_gamma) * sb.s.Bp[k] - sb.s.Cp[k] * inv_gamma) * (sb.a_plus[i] * inv_Bp2[k])
                 - ((1.0f - sb.x[i] * inv_gamma) * sb.s.Bm[k] + sb.s.Cm[k] * inv_gamma) * (sb.a_minus[i] * inv_Bm2[k]);
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
                next_g[i] = joined ? g[i] + g[i + step] : g[i];
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
            if (hit) grad_URAM[b][row] += add;
        }
    }
}

} // namespace plalgo

#endif // PL_ALGO_WA_GRADIENT_HPP
