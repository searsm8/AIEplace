#ifndef PL_ALGO_PARAM_SCHEDULER_HPP
#define PL_ALGO_PARAM_SCHEDULER_HPP

// param_scheduler -- the ePlace schedule + convergence test (XPlace param_scheduler.py analogue)
// on the PL.
//
// One call = the END-of-iteration policy update for iteration k. Given iteration k's two reduced
// metrics (HPWL, overflow) and the two Barzilai-Borwein reduction norms, plus the persistent
// SchedState, it produces the FOUR scalars the next iteration consumes (inv_gamma, alpha=BB step,
// coeff=Nesterov momentum, lambda=density weight) and a STOP flag (convergence / divergence / max
// iters). It is a pure scalar recurrence -- cheap; its value on the PL is loop RESIDENCY (keep the
// whole iteration on-device, no host round-trip), not speed. So it is written plain/sequential and
// verified BIT-FOR-BIT against the sw_only golden, not optimized.
//
// Every formula mirrors a sw_only function (host/src/sw_only/src/AIEplace.cpp / Output.cpp):
//   updateGamma (Partials.cpp:182) .............. inv_gamma
//   computeLipshitzEstimate (:281) .............. alpha
//   performNextStep momentum (:855) ............. coeff (+ nesterov_ak recurrence)
//   updateDensityWeight (:324) .................. lambda (trend mu + overflow-plateau 2x jolt)
//   initializeDensityWeight (:611) .............. lambda on iteration 1
//   recordIterationResults best-tracking (Output.cpp:690) ... best_primary / best_fallback
//   checkConvergence (:652) + checkDivergence (:418) ........ stop flag (countdown + life guard)
// See MARK_TO_REVIEW/PL_PORT_PLAN.md sec 3. Precond is OFF in the tuned config, so density_force_
// fraction has the closed form dff = c*lambda/(1 + c*lambda), c a single host-precomputed scalar.
//
// Scalar C++ (cmath only) so it compiles under BOTH g++ (offline sched_verify replays the golden
// schedule_trace.csv) and Vitis HLS (the on-device call). History windows use small fixed rings.

#include <cmath>

namespace plalgo {

static const int SCHED_RING = 64; // history depth (>= the largest window: checkOverflowPlateau(50))

struct SchedState {
    // --- schedule ---
    float lambda;       // density weight in use (updated in place to the next iteration's value)
    float inv_gamma;    // held 1/gamma; recomputed only on non-skipped iterations (shared skip_update)
    float nesterov_ak;  // Nesterov a_k accumulator (sw_only starts at 1.0)
    float prev_hpwl;    // previous iteration's HPWL (trend sign for the lambda mu)
    int   iteration;    // 0 before the first call; the call for iteration k sets it to k
    int   last_jolt_iter;   // last overflow-plateau 2x jolt iteration
    // --- convergence / best tracking ---
    float bp_hpwl, bp_ovfw; int bp_valid;   // best_primary (lowest HPWL with overflow < stop)
    float bf_hpwl, bf_ovfw; int bf_valid;   // best_fallback (lowest overflow, Pareto)
    int   life;             // divergence-guard budget (starts MAX_LIFE)
    int   conv_remaining;   // overflow-below-stop countdown (-1 until first crossing)
    // --- history rings (index (k-1) mod SCHED_RING) ---
    float hpwl_ring[SCHED_RING];
    float ovfw_ring[SCHED_RING];
};

struct SchedParams {
    float base_gamma;        // grid-independent WA base (AIEplace.cpp finalize block)
    float min_step;          // density_weight_min_step (0.95)
    float max_step;          // density_weight_max_step (1.05)
    float init_multiplier;   // density_weight_init_multiplier (8e-5)
    float dff_coef;          // c = precond_coef*K/total_pins; dff = c*lambda/(1+c*lambda)
    int   enable_momentum;   // 0/1
    int   gamma_schedule;    // 0/1; if 1 initial gamma = 10*base_gamma (else base_gamma)
    // convergence
    float overflow_threshold; // masked-overflow stop (0.04)
    int   min_iters;          // convergence_min_iterations (50)
    int   max_iters;          // convergence_max_iterations (1200)
    int   conv_iters;         // convergence_iterations countdown (30)
    int   max_life;           // MAX_LIFE (30)
};

static const int SCHED_BEST_MIN_ITER = 50; // best-tracking skips iters < this (BEST_SOL_MIN_ITER)

// density_force_fraction closed form (precond OFF): dff = c*lambda/(1 + c*lambda), c = dff_coef =
// precond_coef*K/total_pins (host-precomputed; K = sum of movable+filler normalized areas). This
// REPLACES sw_only's per-node updatePrecondWeights reduction. It is the caller's job to compute dff
// with this and pass it into param_scheduler -- mirroring sw_only, where updateDensityWeight consumes
// the density_force_fraction that updatePrecondWeights produced. Verified separately from the trend.
static inline float sched_dff(float lambda, float dff_coef) {
    const float cl = dff_coef * lambda;
    return cl / (1.0f + cl);
}

static inline void sched_state_init(SchedState& st, const SchedParams& p) {
    st.lambda = 1.0f; st.nesterov_ak = 1.0f; st.prev_hpwl = 0.0f; st.iteration = 0;
    // Initial gamma matches the golden ctor: 10x base under the schedule, else base. Held until the
    // first non-skipped iteration recomputes it (gamma is now throttled by the shared skip_update).
    st.inv_gamma = 1.0f / ((p.gamma_schedule ? 10.0f : 1.0f) * p.base_gamma);
    st.last_jolt_iter = -1000000;
    st.bp_hpwl = st.bp_ovfw = 1e30f; st.bp_valid = 0;
    st.bf_hpwl = st.bf_ovfw = 1e30f; st.bf_valid = 0;
    st.life = p.max_life; st.conv_remaining = -1;
    for (int i = 0; i < SCHED_RING; i++) { st.hpwl_ring[i] = 0.0f; st.ovfw_ring[i] = 0.0f; }
}

// (max-min)/mean of the last N overflow samples < threshold  (checkOverflowPlateau, AIEplace.cpp:397)
static inline bool sched_ovfw_plateau(const SchedState& st, int k, int N, float threshold) {
    if (k < N) return false;
    float mn = 1e30f, mx = -1e30f, sum = 0.0f;
    for (int j = 0; j < N; j++) {
        const float v = st.ovfw_ring[(k - 1 - j) % SCHED_RING];
        if (v < mn) mn = v; if (v > mx) mx = v; sum += v;
    }
    return (mx - mn) / (sum / N + 1e-8f) < threshold;
}

// checkDivergence (AIEplace.cpp:418), window=3. References best_primary only (bp_*).
static inline bool sched_divergence(const SchedState& st, const SchedParams& p, int k,
                                    float overflow, float threshold) {
    const int W = 3;
    if (!st.bp_valid) return false;
    if (k <= W) return false;
    float wl_sum = 0.0f;
    for (int j = 0; j < W; j++) wl_sum += st.hpwl_ring[(k - 1 - j) % SCHED_RING];
    const float wl_mean = wl_sum / W;
    const float wl_ratio = (wl_mean - st.bp_hpwl) / (st.bp_hpwl + 1e-8f);
    if (wl_ratio <= threshold * 1.2f) return false;   // HPWL still near best -> not diverging
    float ov_sum = 0.0f, ov_min = 1e30f, ov_max = -1e30f; int rises = 0;
    float prev = st.ovfw_ring[(k - W) % SCHED_RING];
    for (int j = 0; j < W; j++) {
        const float v = st.ovfw_ring[(k - W + j) % SCHED_RING];
        ov_sum += v; if (v < ov_min) ov_min = v; if (v > ov_max) ov_max = v;
        if (j > 0 && v > prev) rises++;
        prev = v;
    }
    const float ov_mean = ov_sum / W;
    const float up_frac = (float)rises / (W - 1);
    const float best_ov = st.bp_ovfw > p.overflow_threshold ? st.bp_ovfw : p.overflow_threshold;
    const float ov_ratio = (ov_mean - best_ov) / (st.bp_ovfw + 1e-8f);
    if (ov_ratio > threshold) return true;                          // overflow grew past best
    if ((ov_max - ov_min) / (ov_mean + 1e-8f) < threshold) return true; // plateaued high
    if (up_frac > 0.6f) return true;                                // fluctuating upward
    return false;
}

// Process iteration k. Inputs are iteration k's reductions; outputs are what iteration k+1 uses,
// plus *stop (true once the run should end). iter1_gwl_L1 / iter1_gden_L1 feed the lambda init.
static void param_scheduler(
    SchedState& st, const SchedParams& p,
    float hpwl, float overflow,              // metrics reduce (this iteration)
    float pos_norm_sq, float grad_norm_sq,   // BB reduce (this iteration)
    float dff,                               // density_force_fraction = sched_dff(lambda, dff_coef)
    float iter1_gwl_L1, float iter1_gden_L1, // iteration-1 lambda init only
    float& inv_gamma_out, float& alpha_out, float& coeff_out, float& lambda_out, int& stop_out)
{
    const int k = st.iteration + 1;
    st.iteration = k;
    st.hpwl_ring[(k - 1) % SCHED_RING] = hpwl;   // record every iteration (history append)
    st.ovfw_ring[(k - 1) % SCHED_RING] = overflow;

    // ===== best-solution tracking (recordIterationResults, Output.cpp:690) =====
    if (k >= SCHED_BEST_MIN_ITER) {
        const float OVFW_EPS = 0.005f;
        if (overflow < p.overflow_threshold && hpwl < st.bp_hpwl) {
            st.bp_hpwl = hpwl; st.bp_ovfw = overflow; st.bp_valid = 1;
        }
        if (overflow < st.bf_ovfw - OVFW_EPS ||
            (overflow < st.bf_ovfw + OVFW_EPS && hpwl < st.bf_hpwl)) {
            st.bf_hpwl = hpwl; st.bf_ovfw = overflow; st.bf_valid = 1;
        }
    }

    // ===== schedule =====
    // Shared skip_update gate (matches the golden performIteration): on 2 of every 3 iterations in
    // the early stage (k<50) or while the WL/density forces are mid-balance (dff in (0.5,0.95)),
    // FREEZE both gamma and lambda together (XPlace step() freezes lambda, gamma, precond together).
    const bool skip_update = ((k < 50) || (dff > 0.5f && dff < 0.95f)) && (k % 3 != 0);

    // (a) gamma: gamma = 10^((overflow-0.1)*20/9 - 1) * base_gamma; recomputed only when not skipping,
    // else held at its previous value (throttled with lambda). inv_gamma is used the NEXT iteration.
    if (!skip_update) {
        const float coef = std::pow(10.0f, (overflow - 0.1f) * (20.0f / 9.0f) - 1.0f);
        st.inv_gamma = 1.0f / (coef * p.base_gamma);
    }
    inv_gamma_out = st.inv_gamma;

    // (b) alpha: clamp(sqrt(||dv||^2)/sqrt(||dg||^2 + 1e-8), 1e-4, 4000)
    float alpha = std::sqrt(pos_norm_sq) / std::sqrt(grad_norm_sq + 1e-8f);
    if (alpha < 1e-4f)  alpha = 1e-4f;
    if (alpha > 4000.f) alpha = 4000.f;
    alpha_out = alpha;

    // (c) coeff: Nesterov momentum recurrence on nesterov_ak
    const float a_next = (1.0f + std::sqrt(4.0f * st.nesterov_ak * st.nesterov_ak + 1.0f)) / 2.0f;
    coeff_out = p.enable_momentum ? (st.nesterov_ak - 1.0f) / a_next : 0.0f;
    st.nesterov_ak = a_next;

    // (d) lambda
    if (k == 1) {
        st.lambda = (iter1_gwl_L1 / (iter1_gden_L1 + 1e-8f)) * p.init_multiplier;
    } else {
        // dff is supplied by the caller (sched_dff on the current lambda), mirroring sw_only where
        // updateDensityWeight reads the density_force_fraction updatePrecondWeights already computed.
        // Same skip_update gate as gamma above -- the two are frozen together.
        if (!skip_update) {
            const float dHPWL = hpwl - st.prev_hpwl;
            float mu;
            if (dHPWL < 0.0f) {
                const float decay = std::pow(0.9999f, (float)k);
                mu = p.max_step * (decay > 0.98f ? decay : 0.98f);
            } else {
                const float rel = dHPWL / (st.prev_hpwl + 1e-8f);
                float g = std::pow(p.max_step, -rel * 100.0f);
                if (g < p.min_step) g = p.min_step;
                if (g > p.max_step) g = p.max_step;
                mu = p.max_step * g;
            }
            st.lambda *= mu;
            // overflow-plateau 2x jolt (fires at most once per ~1000 iters)
            if (k > 25 && (k - st.last_jolt_iter) >= 1000 &&
                overflow > 0.7f && sched_ovfw_plateau(st, k, 25, 0.001f)) {
                st.lambda *= 2.0f; st.last_jolt_iter = k;
            }
        }
    }
    st.prev_hpwl = hpwl;
    lambda_out = st.lambda;

    // ===== convergence / divergence (checkConvergence, AIEplace.cpp:652) =====
    int stop = 0;
    if (k >= p.max_iters) {
        stop = 1;                                   // max-iter backstop
    } else if (k < p.min_iters) {
        stop = 0;                                   // enforce minimum
    } else if (std::isnan(overflow) || std::isnan(hpwl)) {
        stop = 1;                                   // NaN
    } else {
        // best_ref = best_primary if valid, else best_fallback, else invalid
        const int   ref_valid = st.bp_valid ? 1 : st.bf_valid;
        const float ref_hpwl  = st.bp_valid ? st.bp_hpwl : st.bf_hpwl;
        // coarse backstop: HPWL blown past 2x best
        if (ref_valid && hpwl > 2.0f * ref_hpwl) {
            stop = 1;
        } else {
            // fine guard, armed only in the near-converged band
            if (k > 100 && ref_valid && overflow < 5.0f * p.overflow_threshold) {
                if (sched_divergence(st, p, k, overflow, 0.01f * overflow)) st.life -= 6;
                if (overflow >= p.overflow_threshold && sched_ovfw_plateau(st, k, 50, 0.05f))
                    st.life -= p.max_life;
                if (st.life <= 0) stop = 1;
            }
            // overflow countdown
            if (!stop) {
                if (overflow < p.overflow_threshold) {
                    if (st.conv_remaining < 0) st.conv_remaining = p.conv_iters;
                    st.conv_remaining--;
                    if (st.conv_remaining <= 0) stop = 1;   // converged
                } else if (st.conv_remaining >= 0) {
                    st.conv_remaining = -1;                 // rose back above; reset
                }
            }
        }
    }
    stop_out = stop;
}

} // namespace plalgo

#endif // PL_ALGO_PARAM_SCHEDULER_HPP
