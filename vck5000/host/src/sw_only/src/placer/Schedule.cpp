/**
 * @file Schedule.cpp
 * @brief gamma/lambda schedule policy and convergence/divergence checks. Split out of
 *        AIEplace.cpp.
 */

#include "AIEplace.h"
#include <cmath>
#include <algorithm>
#include <numeric>

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief XPlace param_scheduler.step(): one shared perform_update flag throttles the
 *        density-weight, gamma (wa_coeff), and precond-coef updates together — freezing all
 *        three on 2 of every 3 iterations during the early stage (iter<50) or while the
 *        placement is mid-spread. sw_only previously scoped this gate to density_weight only,
 *        letting gamma sharpen 3x too fast early and over-clumping the cells.
 *
 * ### The "mid-spread" test reads precond_kappa, NOT a gradient ratio — TODO #19b
 * XPlace tests `weighted_weight` (== our `precond_kappa`), the PRECONDITIONER mass ratio.
 * sw_only used to test `density_force_fraction`, a GRADIENT-norm ratio, under a doc-comment
 * claiming it was XPlace's quantity. They are different functions and the difference is not
 * cosmetic: κ carries λ linearly, so it crosses (0.5, 0.95) once and leaves, releasing the
 * throttle for the endgame; the gradient ratio is not monotone in λ (∇den falls as cells spread),
 * so it drifts INTO the window late and holds the 3x throttle on exactly when λ must ramp.
 * Measured, MMS adaptec1 phase-2 endgame: λ grows x68 per 100 iterations in XPlace, x11 under the
 * gradient ratio, x20->x105 under κ. Over 16 MMS designs the fix (with the filler retraction in
 * Output.cpp, which does not work alone) took post-DP HPWL vs XPlace +1.23% -> +0.97% and runs
 * that `converged` 6/16 -> 15/16. A `schedule_gate_metric` config selected between the two for
 * the A/B; deleted 2026-08-07 once settled, per the retire-settled-toggles pattern (TODO #2).
 * `density_force_fraction` is still computed and logged below — it is just not the gate.
 */
void Placer::updateSchedule()
{
    // Phase-relative: XPlace gates all three on `self.iter - self.init_iter`
    // (param_scheduler.py:285-288), so a new phase re-enters its own warmup.
    bool past_warmup      = (phaseIteration() >= 50);
    bool mid_spread       = (precond_kappa > 0.5f && precond_kappa < 0.95f);
    bool every_third_iter = (phaseIteration() % 3 == 0);

    bool perform_update = every_third_iter || (past_warmup && !mid_spread);
    if (perform_update) {
        updateGamma(ovfw_history.back());
        updateDensityWeight();
    }

    // Both gate candidates, every 50 iterations: the throttle is the difference between a lambda
    // that ramps and one that crawls, and which quantity is in the window is otherwise invisible.
    if (phaseIteration() % 50 == 0)
        Logger::log_detail("[GATE] iter=" + std::to_string(iteration) +
                           " kappa=" + PREC(precond_kappa) +
                           " force_frac=" + PREC(density_force_fraction) +
                           " throttled=" + std::string(perform_update ? "no" : "yes"));
}

/**
 * @brief Finalize the WA smoothing length. gamma is a physical length (the softmax temperature
 *        of the WA HPWL surrogate); the optimal ABSOLUTE gamma tracks the layout, not the bin
 *        count. XPlace ties base_gamma to bin size (base_gamma = wa_coeff*(unit_len_x+unit_len_y),
 *        param_scheduler.py) which is proportional to 1/N; that over-sharpens at fine grids for
 *        gamma-sensitive designs (adaptec2@1024 lost ~12% HPWL vs @512). We instead reference the
 *        bin geometry to a FIXED grid (gamma_ref_grid, default 512) so base_gamma is
 *        grid-INDEPENDENT: base_gamma = init_gamma*(die_w+die_h)/gamma_ref_grid. At the reference
 *        grid this equals the bin-tied form exactly (bin_w+bin_h == die_span/gamma_ref_grid), so
 *        the tuned @512 suite is unchanged; at other resolutions the absolute gamma is preserved.
 *        sw_only's init_gamma plays XPlace's wa_coeff role. gamma_bin_scaled=false = legacy bare
 *        constant. gamma_schedule starts gamma at 10x base (overflow~1) and shrinks it as overflow
 *        drops (updateGamma).
 */
void Placer::configureGammaSchedule()
{
    if (gamma_bin_scaled)
        base_gamma = base_gamma * (grid.getDieWidth() + grid.getDieHeight()) / gamma_ref_grid;
    gamma     = gamma_schedule ? 10.0f * base_gamma : base_gamma;
    inv_gamma = 1.0f / gamma;
    if (partials_method == "simple")
        initHpwlLut();
    Logger::log_detail("WA gamma: base_gamma=" + std::to_string(base_gamma) +
                     " (bin_scaled=" + std::string(gamma_bin_scaled ? "true" : "false") +
                     "), initial gamma=" + std::to_string(gamma));
}

/**
 * @brief Update density_weight to increase density force over time.
 * Numerically analyze the current health of the optimization:
 * Is HPWL improving?
 * If yes, continue increasing density_weight slowly.
 * If not, slow or decrease density_weight to allow HPWL forces to have more influence and escape local minima.
 */
void Placer::updateDensityWeight()
{
    if (hpwl_history.size() < 2) return; // need a previous HPWL to measure the trend

    // XPlace step_density_weight (param_scheduler.py). The every-3rd-iteration slow-phase throttle
    // is now applied by the shared skip_update flag in performIteration (matching Xplace step(),
    // which freezes λ, gamma, and precond_coef together), so this function updates unconditionally
    // once called.
    float current_hpwl = hpwl_history.back();
    float prev_hpwl    = hpwl_history[hpwl_history.size() - 2];
    float delta_hpwl   = current_hpwl - prev_hpwl;

    float dw_min_step = ConfigUtils::require<float>(cfg, "params", "density_weight_min_step"); // μ lower clamp (0.95)
    float dw_max_step = ConfigUtils::require<float>(cfg, "params", "density_weight_max_step"); // μ growth base (1.05)

    // μ > 1 grows λ. Grow near the max rate (decaying toward 0.98·max) while wirelength is
    // still improving; damp toward ~1.0 once wirelength worsens, so density does not
    // overshoot and blow HPWL up (the late-run divergence observed before this change).
    //
    // Worsening branch damping — must be SCALE-INVARIANT. Default = relative form
    // (mu ~ 1.05^(-(delta_hpwl/prev_hpwl)*100)): delta_hpwl/prev_hpwl is dimensionless, so the
    // damping behaves the same regardless of a design's absolute HPWL magnitude.
    //
    // History: a fixed constant K=350000 was tried to match XPlace step_density_weight
    // (param_scheduler.py:280) exactly, assuming sw_only's raw-DBU HPWL is XPlace's
    // round(hpwl*die_scale/site_width) frame (~1e7-1e8). That assumption is FALSE for standard-cell
    // designs (raw HPWL 5e8-1e9): delta_hpwl/350000 becomes O(1), mu pins to the 0.95 floor, and
    // lambda can never ramp -> the placement collapses and never spreads (des_perf/edit_dist/
    // superblue stalls at overflow ~0.8). So the fixed-K default is reverted. Config
    // `density_weight_worsening_hpwl_norm` still exposes K: set it >0 to use the fixed-K form (only
    // correct if delta_hpwl is first site-width-normalized into XPlace's frame).
    float worsening_hpwl_norm = cfg["params"]["density_weight_worsening_hpwl_norm"].value_or(-1.0f);
    float mu;
    if (delta_hpwl < 0.0f) {
        // Phase-relative decay (XPlace param_scheduler.py:307). Phase 2 restarts the ramp
        // rather than inheriting phase 1's already-decayed mu.
        mu = dw_max_step * std::max(std::pow(0.9999f, (float)phaseIteration()), 0.98f);
    } else if (worsening_hpwl_norm > 0.0f) {
        mu = dw_max_step * std::clamp(std::pow(dw_max_step, -delta_hpwl / worsening_hpwl_norm),
                                      dw_min_step, dw_max_step);
    } else {
        float rel_worsening = delta_hpwl / (prev_hpwl + 1e-8f);
        mu = dw_max_step * std::clamp(std::pow(dw_max_step, -rel_worsening * 100.0f),
                                      dw_min_step, dw_max_step);
    }
    // NO UPPER CLAMP ON lambda, and that is now a deliberate decision rather than an oversight
    // (TODO #4 "density-weight runaway", investigated 2026-08-04). XPlace has none either
    // (step_density_weight, param_scheduler.py:299-310).
    //
    // The runaway was real: on the pre-#11b adaptec5, iterations 592 -> 1163 held overflow pinned
    // at 0.423 while lambda went 0.0504 -> 2.84e4 (x564,000) and HPWL doubled, which tripped the
    // 2x-best divergence test and cost the run its phase 2. But it was a SYMPTOM. Making the
    // movable-macro deposit unconditional (TODO #11b, landed 2026-08-02) removes the overflow floor
    // that starved the feedback loop, and adaptec5's phase 1 now CONVERGES at iteration 649
    // instead. No design in the suite exhibits the runaway once that is in.
    //
    // A freeze-while-overflow-is-flat rule was built and measured before that was understood. It is
    // a trap and must not be re-added without an escape mechanism: freezing lambda on a plateau is
    // self-reinforcing, because lambda is exactly what ends the plateau. adaptec5 deadlocked and
    // exited phase 1 at iteration 401 with exact overflow 0.89, completely unspread. The 2x jolt
    // below IS the sanctioned escape, and it is confined to the high-overflow band on purpose.
    density_weight *= mu;

    // Emergency 2x jolt: if overflow has plateaued at a high value, double density_weight
    // to break out of the stall. Modeled after XPlace's enlarge_density mechanism.
    // (param_scheduler.py lines 293-304)
    int plateau_window = ConfigUtils::require<int>(cfg, "params", "adaptation_window");
    float plateau_threshold = ConfigUtils::require<float>(cfg, "params", "slow_improvement_threshold");
    float high_ovfw = ConfigUtils::require<float>(cfg, "params", "high_overflow_threshold");
    // Xplace uses 1000 (effectively once per run). Lower it to let the jolt re-fire and
    // repeatedly double lambda through a stall (a deliberate deviation from Xplace).
    int min_jolt_interval = cfg["params"]["density_jolt_interval"].value_or(1000);

    bool past_warmup          = (phaseIteration() > plateau_window);
    // Cooldown stays on absolute `iteration`: it measures elapsed time since the last jolt,
    // and last_density_jolt_iter is recorded in the same absolute frame.
    bool jolt_cooldown_expired = (iteration - last_density_jolt_iter >= min_jolt_interval);
    bool overflow_high        = (ovfw_history.back() > high_ovfw);
    bool is_plateaued         = checkOverflowPlateau(plateau_window, plateau_threshold);

    bool should_jolt = past_warmup && jolt_cooldown_expired && overflow_high && is_plateaued;
    if (should_jolt)
    {
        density_weight *= 2.0f;
        last_density_jolt_iter = iteration;
        Logger::log_detail("Overflow plateau detected (ovfw=" +
            PREC(ovfw_history.back()) + "), 2x density weight jolt -> " +
            PREC(density_weight));
    }

    // Escalate preconditioner: double precond_coef every 20 iterations once overflow < 0.3
    // This progressively tightens macro movement in late placement (XPlace param_scheduler.py:340-347)
    bool escalation_enabled = (enable_preconditioning && precond_coef_escalation);
    bool overflow_low       = (ovfw_history.back() < 0.3f);
    bool coef_below_cap     = (precond_coef < 1024.0f);

    bool should_escalate_precond = escalation_enabled && overflow_low && coef_below_cap;
    if (should_escalate_precond) {
        // Phase-relative (XPlace param_scheduler.py:365: `(self.iter - self.init_iter) % 20`).
        if (phaseIteration() % 20 == 0) {
            precond_coef *= 2.0f;
            Logger::log_detail("Preconditioner escalation: precond_coef=" + PREC(precond_coef));
        }
    }
}


/**
 * @brief Check if overflow has plateaued over a recent window.
 *
 * Returns true if the relative range (max - min) / mean of the last
 * `window` overflow values is below `threshold`. Matches XPlace's
 * check_plateau() in param_scheduler.py.
 */
bool Placer::checkOverflowPlateau(int window, float threshold)
{
    if ((int)ovfw_history.size() < window) return false;
    auto begin = ovfw_history.end() - window;
    auto end = ovfw_history.end();
    float min_val = *std::min_element(begin, end);
    float max_val = *std::max_element(begin, end);
    float mean_val = std::accumulate(begin, end, 0.0f) / window;
    return (max_val - min_val) / (mean_val + 1e-8f) < threshold;
}


/**
 * @brief Detect divergence, mirroring XPlace check_divergence (param_scheduler.py).
 *
 * Fires only once the recent HPWL mean has climbed meaningfully above the best known
 * HPWL (so the healthy early phase, where HPWL rises as cells spread, is not flagged)
 * AND overflow is no longer making progress — it has grown past its best, plateaued
 * high, or is fluctuating upward. Reference is the primary best (converged) if we have
 * one, else the lowest-overflow fallback so the guard is useful even before convergence.
 */
bool Placer::checkDivergence(int window, float threshold)
{
    // Reference the CONVERGED best only (XPlace check_divergence returns False while
    // best_metric["hpwl"] is inf). best_aux tracks the newest lowest-overflow point,
    // so on a healthy monotonic descent a trailing-mean-vs-newest comparison always reads
    // "worse than best" on both HPWL and overflow and false-fires the guard — that killed
    // adaptec2 at iter 332 with overflow still dropping ~2%/iter toward the 0.07 threshold.
    if (!best_primary.valid) return false;
    const BestSolution& best = best_primary;
    if ((int)hpwl_history.size() <= window) return false;

    auto hpwl_begin = hpwl_history.end() - window;
    float wl_mean = std::accumulate(hpwl_begin, hpwl_history.end(), 0.0f) / window;
    float wl_ratio = (wl_mean - best.hpwl) / (best.hpwl + 1e-8f);
    if (wl_ratio <= threshold * 1.2f)
        return false;  // HPWL still near its best → not diverging

    // HPWL is rising above best; classify by how overflow behaves over the window.
    auto ovfw_begin = ovfw_history.end() - window;
    float ovfw_mean = std::accumulate(ovfw_begin, ovfw_history.end(), 0.0f) / window;
    float ovfw_min  = *std::min_element(ovfw_begin, ovfw_history.end());
    float ovfw_max  = *std::max_element(ovfw_begin, ovfw_history.end());
    int rises = 0;
    for (auto it = ovfw_begin + 1; it != ovfw_history.end(); ++it)
        if (*it > *(it - 1)) rises++;
    float ovfw_up_frac = (float)rises / (window - 1);

    float ovfw_ratio = (ovfw_mean - std::max(overflow_threshold, best.overflow)) /
                       (best.overflow + 1e-8f);

    if (ovfw_ratio > threshold)                                  return true; // overflow grew past best
    if ((ovfw_max - ovfw_min) / (ovfw_mean + 1e-8f) < threshold) return true; // plateaued high
    if (ovfw_up_frac > 0.6f)                                     return true; // fluctuating upward
    return false;
}


/**
 * @brief Update per-node preconditioner weights (diagonal preconditioner).
 *
 * Each node's gradient is divided by its precond_weight before the Nesterov step.
 * weight = max(1.0, num_pins + precond_coef * density_weight * area)
 *
 * Large macros (many pins, large area) get heavy damping, while standard cells
 * are barely affected. The precond_coef escalates over time (see updateDensityWeight),
 * progressively tightening macro movement as placement matures.
 *
 * Reference: XPlace param_scheduler.py:349-364, calculator.py:5-8
 */
void Placer::updatePrecondWeights()
{
    TIME_FUNCTION();
    float lambda_area_coef = precond_coef * density_weight;

    // Accumulate the two force-mass components for density_force_fraction:
    //   a1 = wirelength mass (pin count per node), a2 = density mass (λ · normalized area).
    // density_force_fraction = ‖a2‖₁ / (‖a1‖₁ + ‖a2‖₁) ∈ [0,1] measures how balanced the
    // wirelength and density forces are — the scale-invariant progress signal that drives
    // the density-weight schedule (XPlace param_scheduler.update_precond_weight, "weighted_weight").
    // Computed even when preconditioning is disabled, since the schedule still consumes it.
    float a1_norm = 0.0f, a2_norm = 0.0f;

    // Area term for a2: RAW node area, matching XPlace alpha_2 = pcoef·λ·mov_node_area — the
    // coordinate-scale-invariant form (sw_only runs in the same raw-DBU frame as XPlace).
    // One loop over movable-then-filler so a2_norm accumulates in the original order (see
    // DataBase::mv_movable_nodes); fillers carry no pins, so they add no wirelength mass.
    const auto& nodes = db.getMovableNodes();
    const int filler_start = db.getFillerStartIndex();

    m_ordered_reduce.sum2((int)nodes.size(),
        [&](int i, float& a1_term, float& a2_term) {
            Node* node_p = nodes[i];
            float num_pins = (i < filler_start) ? (float)node_p->getNets().size() : 0.0f;
            float a2 = lambda_area_coef * node_p->getArea();
            a1_term = num_pins;
            a2_term = a2;
            if (enable_preconditioning)
                node_p->precond_weight = std::max(1.0f, num_pins + a2);
        }, a1_norm, a2_norm);

    // XPlace's weighted_weight, verbatim (param_scheduler.py:386): the ratio of the two
    // PRECONDITIONER addend masses. alpha_2 carries λ linearly, so κ rises monotonically with λ,
    // crosses the (0.5, 0.95) throttle window once, and then stays above it -- which is what lets
    // XPlace's λ ramp at the full μ every iteration in the endgame.
    precond_kappa = a2_norm / (a1_norm + a2_norm + 1e-8f);

    // density_force_fraction: force-magnitude ratio ‖λ·∇den‖₁ / (‖∇wl‖₁ + ‖λ·∇den‖₁), invariant to the
    // field-normalization constant. Uses the PREVIOUS iteration's committed gradient L1 norms (last_g*_L1,
    // refreshed each combineGradients / seeded by initializeDensityWeight on iteration 1), since
    // updatePrecondWeights runs before this iteration's performNextStep→combineGradients.
    // NOT the same function as κ above: it is a ratio of GRADIENT norms, and ∇den falls as cells
    // spread, so it is not monotone in λ. See updateSchedule() for which one gates the schedule.
    density_force_fraction = last_gden_L1 / (last_gwl_L1 + last_gden_L1 + 1e-8f);

    precond_a1_norm = a1_norm; // instrumentation: expose the two preconditioner addend norms for the trace
    precond_a2_norm = a2_norm;
}


/**
 * @brief Check if the placement algorithm has converged
 *
 * Convergence is determined by two criteria that must both be met:
 * 1. Overflow is below the threshold (relative to total cell area)
 * 2. HPWL improvement over a window of iterations is below the threshold
 *
 * Also enforces minimum iteration count and uses maximum iterations as a fallback.
 *
 * @return true if converged (or max iterations reached), false otherwise
 */
bool Placer::checkConvergence()
{
    // Phase-relative floor: phase 2 must serve its own minimum, not inherit phase 1's count.
    if (phaseIteration() < min_iterations)  return false;
    if (reachedMaxIterations())      return true;   // absolute — whole-run runaway backstop
    if (hasNaNMetrics())             return true;
    if (hasCoarseDivergence())       return true;
    if (checkFineDivergenceGuard())  return true;

    return checkOverflowCountdown();
}

/// @brief Safety fallback: stop once iteration reaches max_iterations.
bool Placer::reachedMaxIterations()
{
    if (iteration < max_iterations) return false;
    m_stop_reason = StopReason::MAX_ITERATIONS;
    Logger::log_info("Stopping: reached maximum iterations (" +
                    std::to_string(max_iterations) + ")");
    return true;
}

/// @brief NaN in overflow or HPWL — hard stop.
bool Placer::hasNaNMetrics()
{
    if (!std::isnan(ovfw_history.back()) && !std::isnan(hpwl_history.back())) return false;
    m_stop_reason = StopReason::NAN_METRICS;
    Logger::log_info("Stopping: NaN detected at iteration " + std::to_string(iteration));
    return true;
}

/// @brief The divergence guards' reference metric — deliberately NOT the shipping rule. This one
///        feeds stopping criteria, so its population must stay as wide as it was before TODO #24
///        (any tracker, converged or not); selectBestSolution() answers the different question of
///        which placement to hand over at the end.
const Placer::BestSolution& Placer::bestReference() const
{
    return best_primary.valid  ? best_primary
         : best_aux.valid      ? best_aux
         : best_rollback.valid ? best_rollback
         : best_primary;
}

/**
 * @brief Which solution to ship. Ported from XPlace get_best_solution (param_scheduler.py:540-577):
 *        a surviving rollback wins outright (it only survives when the run never converged, so
 *        nothing else exists); otherwise PREFER the lower-overflow aux, but only when it costs
 *        <= 0.5% HPWL and buys >= 10% overflow. The default lean is toward the spread-out solution.
 */
Placer::BestChoice Placer::selectBestSolution() const
{
    if (best_rollback.valid)
        return {&best_rollback, BestSlot::ROLLBACK, "rollback (never converged)"};
    if (!best_primary.valid && !best_aux.valid) return {};
    if (!best_aux.valid)     return {&best_primary, BestSlot::PRIMARY, "primary (HPWL driven)"};
    if (!best_primary.valid) return {&best_aux,     BestSlot::AUX,     "aux (overflow driven)"};

    const bool aux_worth_its_hpwl = (best_aux.hpwl < best_primary.hpwl * best_aux_max_hpwl_ratio &&
                                     best_aux.overflow * 1.1f < best_primary.overflow);
    return aux_worth_its_hpwl ? BestChoice{&best_aux,     BestSlot::AUX,     "aux (overflow driven)"}
                              : BestChoice{&best_primary, BestSlot::PRIMARY, "primary (HPWL driven)"};
}

/**
 * @brief Coarse divergence backstop: HPWL has blown past 2x the best known solution WHILE overflow
 *        is getting worse. Both conjuncts are XPlace's (param_scheduler.py need_to_early_stop:
 *        `overflow[ptr] > overflow[ptr-1] and hpwl[ptr] > best_metric["hpwl"] * 2`). The overflow
 *        term is what makes this a divergence test rather than a spreading test — HPWL legitimately
 *        climbs well past 2x best while cells spread, so on the HPWL term alone this fired on
 *        healthy macro-heavy runs (adaptec5, newblue7) that were still descending in overflow.
 */
bool Placer::hasCoarseDivergence()
{
    const BestSolution& best_ref = bestReference();
    float current_hpwl = hpwl_history.back();
    if (!best_ref.valid || current_hpwl <= 2.0f * best_ref.hpwl) return false;
    if (ovfw_history.size() < 2) return false;
    float overflow = ovfw_history.back(), prev_overflow = ovfw_history[ovfw_history.size() - 2];
    if (overflow <= prev_overflow) return false;   // still spreading, not diverging

    m_stop_reason = StopReason::DIVERGED_HPWL;
    Logger::log_info("Stopping: divergence detected at iteration " +
                    std::to_string(iteration) +
                    " (HPWL " + std::to_string(current_hpwl) +
                    " > 2x best " + std::to_string(best_ref.hpwl) +
                    " from iter " + std::to_string(best_ref.iteration) +
                    ", overflow rising " + PREC(prev_overflow) + " -> " + PREC(overflow) + ")");
    return true;
}

/**
 * @brief Fine-grained divergence guard, armed only once the run is in the near-converged
 *        band (overflow < 5x the stop threshold). During the high-overflow spreading phase
 *        HPWL naturally rises and overflow is noisy, which would otherwise be misread as
 *        divergence. Each detection burns 6 life; a hard overflow plateau ends it outright.
 */
bool Placer::checkFineDivergenceGuard()
{
    const BestSolution& best_ref = bestReference();
    float overflow = ovfw_history.back();

    bool guard_armed = (phaseIteration() > 100 && best_ref.valid &&
                        overflow < 5.0f * overflow_threshold);
    if (!guard_armed) return false;

    if (checkDivergence(3, 0.01f * overflow))
        life -= 6;
    // DELIBERATE DIVERGENCE from XPlace, and it is the mixed-size case that needs explaining.
    // XPlace gates this kill on `not self.include_macros` (param_scheduler.py:467) — but that is
    // not leniency: in XPlace, `need_to_early_stop()` firing during phase 1 TRIGGERS macro
    // legalization and the optimizer reset (run_placement_nesterov.py:167-172), then clears the
    // signal and continues. The guard is its phase-1 EXIT, not a kill.
    //
    // We keep the plateau kill active in mixed-size so it ends PHASE 1 at the same point XPlace
    // would have handed off to macro legalization. Disabling it (tried 2026-07-31) just grinds to
    // convergence_max_iterations with the density weight running away — newblue4/newblue5 went
    // 670 -> 1200 iterations for a 0.02 overflow gain and a 45% HPWL inflation.
    //
    // The old note here said "re-gate on `!mixed_size_mode` once TODO #13 lands". #13 landed
    // 2026-08-01 and that re-gate is NOT wanted: `mixed_size_mode` is set false at the phase-2
    // transition (Phase2.cpp), so the kill is already enabled in phase 2 — which is what XPlace
    // does too (`not self.include_macros`, param_scheduler.py:467). Gating on `!mixed_size_mode`
    // would only DISABLE it in phase 1, i.e. remove our phase-1 exit. Left as is, deliberately.
    if (overflow >= overflow_threshold && checkOverflowPlateau(50, 0.05f))
        life -= MAX_LIFE;

    if (life > 0) return false;

    m_stop_reason = StopReason::DIVERGENCE_GUARD;
    Logger::log_info("Stopping: divergence guard exhausted at iteration " +
                    std::to_string(iteration) + " (best HPWL " +
                    std::to_string(best_ref.hpwl) + " from iter " +
                    std::to_string(best_ref.iteration) + ")");
    return true;
}

/**
 * @brief Overflow countdown — XPlace-inspired convergence mechanism. Once overflow drops
 *        below threshold, count down convergence_iterations then stop. The countdown runs
 *        in full after smoothed overflow first crosses the threshold, rather than stopping
 *        on the first crossing: HPWL is already plateaued by this point, so a HPWL-plateau
 *        early-out would stop immediately and leave overflow (and thus the physical spread)
 *        worse than it needs to be. (Mirrors XPlace's post-threshold life.)
 */
bool Placer::checkOverflowCountdown()
{
    float overflow = ovfw_history.back();

    if (overflow >= overflow_threshold) {
        // Overflow rose back above threshold — reset countdown
        if (convergence_iterations_remaining >= 0) {
            Logger::log_detail("Overflow rose above threshold, resetting convergence countdown");
            convergence_iterations_remaining = -1;
        }
        return false;
    }

    if (convergence_iterations_remaining < 0) {
        // First crossing below threshold — start countdown
        convergence_iterations_remaining = convergence_iterations;
        Logger::log_detail("Overflow below " + PREC(overflow_threshold) +
                        ", starting convergence countdown (" +
                        std::to_string(convergence_iterations) + " iterations)");
    }
    convergence_iterations_remaining--;

    if (convergence_iterations_remaining <= 0) {
        m_stop_reason = StopReason::CONVERGED;
        Logger::log_info("Convergence achieved at iteration " +
                        std::to_string(iteration) +
                        " (overflow countdown complete)");
        return true;
    }

    Logger::log_detail("Convergence countdown: " +
                      std::to_string(convergence_iterations_remaining) + " remaining");
    return false;
}

AIEPLACE_NAMESPACE_END
