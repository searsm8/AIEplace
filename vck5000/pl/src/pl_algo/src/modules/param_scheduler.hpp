#ifndef PL_ALGO_PARAM_SCHEDULER_HPP
#define PL_ALGO_PARAM_SCHEDULER_HPP

// param_scheduler -- the ePlace schedule (XPlace param_scheduler.py analogue) on the PL.
//
// One call = the END-of-iteration policy update for iteration k. Given iteration k's two reduced
// metrics (HPWL, overflow) and the two Barzilai-Borwein reduction norms (from iteration_update /
// computeLipshitzEstimate), plus the persistent SchedState, it produces the FOUR scalars the next
// iteration consumes: inv_gamma (HPWL smoothing), alpha (BB step), coeff (Nesterov momentum), and
// lambda (density weight). It is a pure scalar recurrence -- cheap; its value on the PL is loop
// RESIDENCY (keep the whole iteration on-device, no host round-trip), not speed. So it is written
// plain/sequential and verified BIT-FOR-BIT against the markv1 golden, not optimized.
//
// Every formula mirrors a markv1 function (host/src/markv1/src/AIEplace.cpp):
//   updateGamma (Partials.cpp:182) .......... inv_gamma
//   computeLipshitzEstimate (:281) .......... alpha
//   performNextStep momentum (:855) ......... coeff (+ nesterov_ak recurrence)
//   updateDensityWeight (:324) .............. lambda (trend mu; plateau jolt = S4, TODO)
//   initializeDensityWeight (:611) .......... lambda on iteration 1
//   checkConvergence (:652) ................. stop flag (tiers 1-2 = S5, TODO)
// See MARK_TO_REVIEW/PL_PORT_PLAN.md sec 3. Precond is OFF in the tuned config, so density_force_
// fraction has the closed form dff = c*lambda/(1 + c*lambda) with c = precond_coef*K/total_pins a
// single host-precomputed scalar (K = sum of movable+filler normalized areas); see the plan sec 3.2.
//
// This header is scalar C++ (cmath only) so it compiles under BOTH g++ (offline SchedVerify replays
// the golden schedule_trace.csv) and Vitis HLS (the on-device call). No STL in the datapath.

#include <cmath>

namespace plalgo {

// Persistent schedule state (carried across iterations). At Stage 6 this lives on-chip in the
// resident loop; during host-driven bring-up it round-trips as a tiny DDR buffer.
// Overflow-plateau window for the S4 jolt (markv1 adaptation_window = 25).
static const int SCHED_OVFW_WINDOW = 25;

struct SchedState {
    float lambda;       // density weight in use (updated in place to the next iteration's value)
    float nesterov_ak;  // Nesterov a_k accumulator (markv1 starts at 1.0)
    float prev_hpwl;    // previous iteration's HPWL (trend sign for the lambda mu)
    int   iteration;    // 0 before the first call; the call for iteration k sets it to k
    int   last_jolt_iter;   // S4: last overflow-plateau 2x jolt iteration
    float ovfw_ring[SCHED_OVFW_WINDOW]; // S4: last WINDOW overflow values (plateau detection)
    // --- reserved for later stages (kept in the struct so its size is stable) ---
    int   conv_countdown;   // S5: overflow-below-stop countdown (-1 until first crossing)
    int   life;             // S5: divergence-guard budget
};

// Immutable per-run config the scheduler needs (host-set once).
struct SchedParams {
    float base_gamma;        // grid-independent WA base (AIEplace.cpp finalize block)
    float min_step;          // density_weight_min_step (0.95)
    float max_step;          // density_weight_max_step (1.05)
    float init_multiplier;   // density_weight_init_multiplier (8e-5)
    float dff_coef;          // c = precond_coef*K/total_pins; dff = c*lambda/(1+c*lambda)
    int   enable_momentum;   // 0/1
};

static inline void sched_state_init(SchedState& st) {
    st.lambda = 1.0f; st.nesterov_ak = 1.0f; st.prev_hpwl = 0.0f; st.iteration = 0;
    st.last_jolt_iter = -1000000; st.conv_countdown = -1; st.life = 40; // MAX_LIFE placeholder
    for (int i = 0; i < SCHED_OVFW_WINDOW; i++) st.ovfw_ring[i] = 0.0f;
}

// checkOverflowPlateau: true if the relative range (max-min)/mean of the last WINDOW overflow
// values is below `threshold` (markv1 AIEplace.cpp:397). Only valid once WINDOW samples exist.
static inline bool sched_overflow_plateau(const SchedState& st, int k, float threshold) {
    if (k < SCHED_OVFW_WINDOW) return false;
    float mn = st.ovfw_ring[0], mx = st.ovfw_ring[0], sum = 0.0f;
    for (int i = 0; i < SCHED_OVFW_WINDOW; i++) {
        const float v = st.ovfw_ring[i];
        if (v < mn) mn = v; if (v > mx) mx = v; sum += v;
    }
    return (mx - mn) / (sum / SCHED_OVFW_WINDOW + 1e-8f) < threshold;
}

// Process iteration k. Inputs are iteration k's reductions; outputs are what iteration k+1 uses.
// iter1_gwl_L1 / iter1_gden_L1 are only read when st.iteration hits 1 (the lambda-init branch).
static void param_scheduler(
    SchedState& st,
    const SchedParams& p,
    float hpwl, float overflow,          // metrics reduce (this iteration)
    float pos_norm_sq, float grad_norm_sq, // BB reduce (this iteration)
    float iter1_gwl_L1, float iter1_gden_L1, // iteration-1 lambda init only
    float& inv_gamma_out, float& alpha_out, float& coeff_out, float& lambda_out)
{
    const int k = st.iteration + 1;
    st.iteration = k;

    // Record this iteration's overflow in the plateau ring (markv1 appends every iteration to
    // ovfw_history, independent of the lambda cadence gate). Ring index (k-1) mod WINDOW.
    st.ovfw_ring[(k - 1) % SCHED_OVFW_WINDOW] = overflow;

    // (a) gamma -- updateGamma: gamma = 10^((overflow-0.1)*20/9 - 1) * base_gamma; inv_gamma = 1/gamma
    const float coef  = std::pow(10.0f, (overflow - 0.1f) * (20.0f / 9.0f) - 1.0f);
    const float gamma = coef * p.base_gamma;
    inv_gamma_out = 1.0f / gamma;

    // (b) alpha -- computeLipshitzEstimate: clamp(sqrt(||dv||^2)/sqrt(||dg||^2 + 1e-8), 1e-4, 4000)
    float alpha = std::sqrt(pos_norm_sq) / std::sqrt(grad_norm_sq + 1e-8f);
    if (alpha < 1e-4f)  alpha = 1e-4f;
    if (alpha > 4000.f) alpha = 4000.f;
    alpha_out = alpha;

    // (c) coeff -- performNextStep momentum recurrence on nesterov_ak
    const float a_next = (1.0f + std::sqrt(4.0f * st.nesterov_ak * st.nesterov_ak + 1.0f)) / 2.0f;
    coeff_out = p.enable_momentum ? (st.nesterov_ak - 1.0f) / a_next : 0.0f;
    st.nesterov_ak = a_next;

    // (d) lambda
    if (k == 1) {
        // initializeDensityWeight: lambda = (sum|g_wl| / sum|g_density|) * init_multiplier.
        // No trend update on iteration 1 (markv1 returns early: hpwl_history.size() < 2).
        st.lambda = (iter1_gwl_L1 / (iter1_gden_L1 + 1e-8f)) * p.init_multiplier;
    } else {
        // density_force_fraction closed form (precond OFF): dff = c*lambda/(1 + c*lambda).
        const float cl  = p.dff_coef * st.lambda;
        const float dff = cl / (1.0f + cl);
        // cadence gate (updateDensityWeight): slow phase updates only every 3rd iteration.
        const bool slow_phase = (k < 50) || (dff > 0.5f && dff < 0.95f);
        if (!(slow_phase && (k % 3 != 0))) {
            const float dHPWL = hpwl - st.prev_hpwl;
            float mu;
            if (dHPWL < 0.0f) {
                // improving: grow near the max rate, decaying toward 0.98*max
                const float decay = std::pow(0.9999f, (float)k);
                mu = p.max_step * (decay > 0.98f ? decay : 0.98f);
            } else {
                // worsening: damp mu toward ~1 so density does not overshoot and blow up HPWL
                const float rel = dHPWL / (st.prev_hpwl + 1e-8f);
                float g = std::pow(p.max_step, -rel * 100.0f);
                if (g < p.min_step) g = p.min_step;
                if (g > p.max_step) g = p.max_step;
                mu = p.max_step * g;
            }
            st.lambda *= mu;
            // Overflow-plateau 2x jolt (updateDensityWeight, XPlace enlarge_density): once overflow
            // has stalled high, double lambda to break out. Fires at most once per ~1000 iters. On
            // adaptec1 it fires ~iter 114 during the long high-overflow ramp (the WA verify caught it).
            if (k > SCHED_OVFW_WINDOW && (k - st.last_jolt_iter) >= 1000 &&
                overflow > 0.7f && sched_overflow_plateau(st, k, 0.001f)) {
                st.lambda *= 2.0f;
                st.last_jolt_iter = k;
            }
        }
    }
    st.prev_hpwl = hpwl;
    lambda_out = st.lambda;

    // S5 TODO: convergence stop flag (min/max iters + overflow countdown + divergence guards).
}

} // namespace plalgo

#endif // PL_ALGO_PARAM_SCHEDULER_HPP
