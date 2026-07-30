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
 *        wirelength/density forces are mid-balance (density_force_fraction ∈ (0.5,0.95)).
 *        sw_only previously scoped this gate to density_weight only, letting gamma sharpen
 *        3x too fast early and over-clumping the cells.
 */
void Placer::updateSchedule()
{
    bool past_warmup      = (iteration >= 50);
    bool forces_balanced  = (density_force_fraction > 0.5f && density_force_fraction < 0.95f);
    bool every_third_iter = (iteration % 3 == 0);

    bool perform_update = every_third_iter || (past_warmup && !forces_balanced);
    if (perform_update) {
        updateGamma(ovfw_history.back());
        updateDensityWeight();
    }
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
        mu = dw_max_step * std::max(std::pow(0.9999f, (float)iteration), 0.98f);
    } else if (worsening_hpwl_norm > 0.0f) {
        mu = dw_max_step * std::clamp(std::pow(dw_max_step, -delta_hpwl / worsening_hpwl_norm),
                                      dw_min_step, dw_max_step);
    } else {
        float rel_worsening = delta_hpwl / (prev_hpwl + 1e-8f);
        mu = dw_max_step * std::clamp(std::pow(dw_max_step, -rel_worsening * 100.0f),
                                      dw_min_step, dw_max_step);
    }
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

    bool past_warmup          = (iteration > plateau_window);
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
        if (iteration % 20 == 0) {
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
    // best_metric["hpwl"] is inf). best_fallback tracks the newest lowest-overflow point,
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
    for (const auto& item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* node_p = item.second;
        float num_pins = (float)node_p->getNets().size();
        float area = node_p->getArea();
        float a2 = lambda_area_coef * area;
        a1_norm += num_pins;
        a2_norm += a2;
        if (enable_preconditioning)
            node_p->precond_weight = std::max(1.0f, num_pins + a2);
    }
    for (auto filler_p : db.getFillers()) {
        float area = filler_p->getArea();
        float a2 = lambda_area_coef * area;
        a2_norm += a2;  // fillers carry no pins, so they add no wirelength mass
        if (enable_preconditioning)
            filler_p->precond_weight = std::max(1.0f, a2);
    }

    // density_force_fraction: force-magnitude ratio ‖λ·∇den‖₁ / (‖∇wl‖₁ + ‖λ·∇den‖₁), invariant to the
    // field-normalization constant. Uses the PREVIOUS iteration's committed gradient L1 norms (last_g*_L1,
    // refreshed each combineGradients / seeded by initializeDensityWeight on iteration 1), since
    // updatePrecondWeights runs before this iteration's performNextStep→combineGradients.
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
    if (iteration < min_iterations)  return false;
    if (reachedMaxIterations())      return true;
    if (hasNaNMetrics())             return true;
    if (hasCoarseDivergence())       return true;
    if (checkFineDivergenceGuard())  return true;

    return checkOverflowCountdown();
}

/// @brief Safety fallback: stop once iteration reaches max_iterations.
bool Placer::reachedMaxIterations()
{
    if (iteration < max_iterations) return false;
    Logger::log_info("Stopping: reached maximum iterations (" +
                    std::to_string(max_iterations) + ")");
    return true;
}

/// @brief NaN in overflow or HPWL — hard stop.
bool Placer::hasNaNMetrics()
{
    if (!std::isnan(ovfw_history.back()) && !std::isnan(hpwl_history.back())) return false;
    Logger::log_info("Stopping: NaN detected at iteration " + std::to_string(iteration));
    return true;
}

/// @brief The converged (primary) best if valid, else the lowest-overflow fallback.
const Placer::BestSolution& Placer::bestReference() const
{
    return best_primary.valid ? best_primary
         : best_fallback.valid ? best_fallback
         : best_primary;
}

/// @brief Coarse divergence backstop: HPWL has blown past 2x the best known solution.
bool Placer::hasCoarseDivergence()
{
    const BestSolution& best_ref = bestReference();
    float current_hpwl = hpwl_history.back();
    if (!best_ref.valid || current_hpwl <= 2.0f * best_ref.hpwl) return false;

    Logger::log_info("Stopping: divergence detected at iteration " +
                    std::to_string(iteration) +
                    " (HPWL " + std::to_string(current_hpwl) +
                    " > 2x best " + std::to_string(best_ref.hpwl) +
                    " from iter " + std::to_string(best_ref.iteration) + ")");
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
    bool guard_armed = (iteration > 100 && best_ref.valid && overflow < 5.0f * overflow_threshold);
    if (!guard_armed) return false;

    if (checkDivergence(3, 0.01f * overflow))
        life -= 6;
    if (overflow >= overflow_threshold && checkOverflowPlateau(50, 0.05f))
        life -= MAX_LIFE;

    if (life > 0) return false;

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
