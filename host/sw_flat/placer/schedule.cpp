// schedule.cpp -- sw_only's Schedule.cpp (the throttled gamma/lambda/precond schedule and the stop
// tests) and BestSolution.cpp (the three XPlace trackers and the ship rule), phase 1 only. Meow.

#include "placer.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sw_flat {

namespace {

constexpr int   MAX_LIFE = 30;
constexpr int   BEST_SOL_MIN_ITER = 50;
constexpr float AUX_UPDATE_HPWL_RATIO      = 1.005f;   // param_scheduler.py:436
constexpr float AUX_SELECT_OVFW_RATIO      = 1.1f;     // param_scheduler.py:569
constexpr float ROLLBACK_UPDATE_HPWL_RATIO = 1.01f;    // param_scheduler.py:425

// checkOverflowPlateau: relative range of the last `window` overflows below `threshold`. Meow.
bool overflow_plateau(const std::vector<float>& ovfw_history, int window, float threshold)
{
    if ((int)ovfw_history.size() < window) return false;
    auto begin = ovfw_history.end() - window;
    auto end = ovfw_history.end();
    float min_val = *std::min_element(begin, end);
    float max_val = *std::max_element(begin, end);
    float mean_val = std::accumulate(begin, end, 0.0f) / window;
    return (max_val - min_val) / (mean_val + 1e-8f) < threshold;
}

} // namespace

const char* stop_reason_name(StopReason reason)
{
    switch (reason) {
        case StopReason::CONVERGED:        return "converged";
        case StopReason::MAX_ITERATIONS:   return "max_iterations";
        case StopReason::NAN_METRICS:      return "nan_metrics";
        case StopReason::NAN_PARTIALS:     return "nan_partials";
        case StopReason::DIVERGED_HPWL:    return "diverged_hpwl";
        case StopReason::DIVERGENCE_GUARD: return "divergence_guard";
        case StopReason::RUNNING:          break;
    }
    return "unknown";
}

// updatePrecondWeights: weight = max(1, num_pins + precond_coef * lambda * area), and
// precond_kappa = |a2|_1 / (|a1|_1 + |a2|_1), XPlace's weighted_weight (param_scheduler.py:386). Meow.
void Placer::update_precond_weights()
{
    float lambda_area_coef = precond_coef * density_weight;
    float a1_norm = 0.0f, a2_norm = 0.0f;
    for (int i = 0; i < (int)reduce_slots.size(); i++) {
        const long slot = reduce_slots[i];
        float pins = (i < filler_start) ? num_pins[slot] : 0.0f;
        float a2 = lambda_area_coef * area[slot];
        a1_norm += pins;
        a2_norm += a2;
        if (cfg.enable_preconditioning) precond_weight[slot] = std::max(1.0f, pins + a2);
    }
    precond_kappa = a2_norm / (a1_norm + a2_norm + 1e-8f);
    density_force_fraction = last_gden_L1 / (last_gwl_L1 + last_gden_L1 + 1e-8f);
    precond_a1_norm = a1_norm;
    precond_a2_norm = a2_norm;
}

// recordIterationResults: masked HPWL and smoothed overflow at the probe v_k, then XPlace's
// update_best_sol (param_scheduler.py:390-451) from iteration 50. Meow.
void Placer::record_iteration_results()
{
    PhaseTimer timer{time_metrics};
    float hpwl = cfg.hpwl_fixed ? stream_wirelength(true) : total_wirelength(cfg.ignore_net_degree, true);
    float overflow = cfg.overflow_snapshot ? snapshot_overflow : compute_overflow(true);
    hpwl_history.push_back(hpwl);
    ovfw_history.push_back(overflow);
    if (iteration < BEST_SOL_MIN_ITER) return;

    auto snapshot = [&](BestSlot which) {
        for (long slot : movable_slots) best_pos[which][slot] = next.probe_pos[slot];
    };
    const bool converged_now = (overflow < cfg.overflow_threshold);
    if (converged_now && !ever_converged) {
        ever_converged = true;
        best[ROLLBACK] = BestSolution{};
    }
    if (!ever_converged && overflow < 5.0f * cfg.overflow_threshold &&
        hpwl < best[ROLLBACK].hpwl * ROLLBACK_UPDATE_HPWL_RATIO && overflow < best[ROLLBACK].overflow) {
        best[ROLLBACK] = {hpwl, overflow, iteration, true};
        snapshot(ROLLBACK);
    }
    if (converged_now && hpwl < best[AUX].hpwl * AUX_UPDATE_HPWL_RATIO && overflow < best[AUX].overflow) {
        best[AUX] = {hpwl, overflow, iteration, true};
        snapshot(AUX);
    }
    if (converged_now && hpwl < best[PRIMARY].hpwl) {
        best[PRIMARY] = {hpwl, overflow, iteration, true};
        snapshot(PRIMARY);
    }
}

// updateSchedule: XPlace's step() with one shared skip_update for gamma and lambda; the precond
// coefficient steps outside it (param_scheduler.py:359-366). Meow.
void Placer::update_schedule()
{
    bool past_warmup      = (iteration >= 50);
    bool mid_spread       = (precond_kappa > 0.5f && precond_kappa < 0.95f);
    bool every_third_iter = (iteration % 3 == 0);
    bool perform_update = every_third_iter || (past_warmup && !mid_spread);

    if (perform_update) {
        // updateGamma
        if (cfg.gamma_schedule) {
            float coef = std::pow(10.0f, (ovfw_history.back() - 0.1f) * (20.0f / 9.0f) - 1.0f);
            gamma     = coef * base_gamma;
            inv_gamma = 1.0f / gamma;
        }
        // updateDensityWeight
        if (hpwl_history.size() >= 2) {
            float current_hpwl = hpwl_history.back();
            float prev_hpwl    = hpwl_history[hpwl_history.size() - 2];
            float delta_hpwl   = current_hpwl - prev_hpwl;
            const float dw_min_step = cfg.density_weight_min_step, dw_max_step = cfg.density_weight_max_step;
            float mu;
            if (delta_hpwl < 0.0f) {
                mu = dw_max_step * std::max(std::pow(0.9999f, (float)iteration), 0.98f);
            } else if (cfg.density_weight_worsening_hpwl_norm > 0.0f) {
                mu = dw_max_step * std::clamp(std::pow(dw_max_step, -delta_hpwl / cfg.density_weight_worsening_hpwl_norm),
                                              dw_min_step, dw_max_step);
            } else {
                float rel_worsening = delta_hpwl / (prev_hpwl + 1e-8f);
                mu = dw_max_step * std::clamp(std::pow(dw_max_step, -rel_worsening * 100.0f), dw_min_step, dw_max_step);
            }
            density_weight *= mu;

            // The 2x jolt on a high overflow plateau (XPlace enlarge_density). Meow.
            bool jolt = iteration > cfg.adaptation_window &&
                        iteration - last_density_jolt_iter >= cfg.density_jolt_interval &&
                        ovfw_history.back() > cfg.high_overflow_threshold &&
                        overflow_plateau(ovfw_history, cfg.adaptation_window, cfg.slow_improvement_threshold);
            if (jolt) {
                density_weight *= 2.0f;
                last_density_jolt_iter = iteration;
            }
        }
    }

    // updatePrecondCoef. Meow.
    if (cfg.enable_preconditioning && cfg.precond_coef_escalation && ovfw_history.back() < 0.3f &&
        precond_coef < 1024.0f && iteration % 20 == 0)
        precond_coef *= 2.0f;
}

// checkConvergence and its five callees, in sw_only's order. Meow.
bool Placer::check_convergence()
{
    if (iteration < cfg.min_iterations) return false;
    if (iteration >= cfg.max_iterations) { stop_reason = StopReason::MAX_ITERATIONS; return true; }
    if (std::isnan(ovfw_history.back()) || std::isnan(hpwl_history.back())) { stop_reason = StopReason::NAN_METRICS; return true; }

    // bestReference: any tracker, converged or not. Meow.
    const BestSolution& best_ref = best[PRIMARY].valid ? best[PRIMARY] : best[AUX].valid ? best[AUX]
                                 : best[ROLLBACK].valid ? best[ROLLBACK] : best[PRIMARY];
    const float overflow = ovfw_history.back();

    // hasCoarseDivergence (need_to_early_stop): HPWL past 2x best while overflow rises. Meow.
    if (best_ref.valid && hpwl_history.back() > 2.0f * best_ref.hpwl && ovfw_history.size() >= 2 &&
        overflow > ovfw_history[ovfw_history.size() - 2]) {
        stop_reason = StopReason::DIVERGED_HPWL;
        return true;
    }

    // checkFineDivergenceGuard, armed in the near-converged band; checkDivergence references the
    // CONVERGED best only. Meow.
    if (iteration > 100 && best_ref.valid && overflow < 5.0f * cfg.overflow_threshold) {
        const int window = 3;
        const float threshold = 0.01f * overflow;
        bool diverging = false;
        if (best[PRIMARY].valid && (int)hpwl_history.size() > window) {
            const BestSolution& best_sol = best[PRIMARY];
            float wl_mean = std::accumulate(hpwl_history.end() - window, hpwl_history.end(), 0.0f) / window;
            float wl_ratio = (wl_mean - best_sol.hpwl) / (best_sol.hpwl + 1e-8f);
            if (wl_ratio > threshold * 1.2f) {
                auto ovfw_begin = ovfw_history.end() - window;
                float ovfw_mean = std::accumulate(ovfw_begin, ovfw_history.end(), 0.0f) / window;
                float ovfw_min  = *std::min_element(ovfw_begin, ovfw_history.end());
                float ovfw_max  = *std::max_element(ovfw_begin, ovfw_history.end());
                int rises = 0;
                for (auto it = ovfw_begin + 1; it != ovfw_history.end(); ++it)
                    if (*it > *(it - 1)) rises++;
                float ovfw_up_frac = (float)rises / (window - 1);
                float ovfw_ratio = (ovfw_mean - std::max(cfg.overflow_threshold, best_sol.overflow)) / (best_sol.overflow + 1e-8f);
                diverging = ovfw_ratio > threshold || (ovfw_max - ovfw_min) / (ovfw_mean + 1e-8f) < threshold ||
                            ovfw_up_frac > 0.6f;
            }
        }
        if (diverging) life -= 6;
        if (overflow >= cfg.overflow_threshold && overflow_plateau(ovfw_history, 50, 0.05f)) life -= MAX_LIFE;
        if (life <= 0) { stop_reason = StopReason::DIVERGENCE_GUARD; return true; }
    }

    // checkOverflowCountdown: convergence_iterations more once overflow is below the threshold. Meow.
    if (overflow >= cfg.overflow_threshold) {
        convergence_iterations_remaining = -1;
        return false;
    }
    if (convergence_iterations_remaining < 0) convergence_iterations_remaining = cfg.convergence_iterations;
    convergence_iterations_remaining--;
    if (convergence_iterations_remaining <= 0) { stop_reason = StopReason::CONVERGED; return true; }
    return false;
}

// selectBestSolution (XPlace get_best_solution, param_scheduler.py:540-577). Meow.
int Placer::select_best_solution() const
{
    if (best[ROLLBACK].valid) return ROLLBACK;
    if (!best[PRIMARY].valid && !best[AUX].valid) return -1;
    if (!best[AUX].valid) return PRIMARY;
    if (!best[PRIMARY].valid) return AUX;
    const bool aux_worth_its_hpwl = best[AUX].hpwl < best[PRIMARY].hpwl * cfg.aux_select_hpwl_ratio &&
                                    best[AUX].overflow * AUX_SELECT_OVFW_RATIO < best[PRIMARY].overflow;
    return aux_worth_its_hpwl ? AUX : PRIMARY;
}

} // namespace sw_flat
