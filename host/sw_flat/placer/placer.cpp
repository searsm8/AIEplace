// placer.cpp -- sw_only's AIEplace.cpp in the device's loop shape (Mark, 2026-10-27):
//     initialization(); while (running) { HPWL gradient; electric field; iteration_update(); }
// It is sw_only's loop rotated so each iteration starts with its gradient: the same float
// operations in the same order, so the run stays bit-identical (check-placer). The schedule trace
// (dumpScheduleTrace) is in sw_only's exact format. Meow.

#include "placer.hpp"

#include <experimental/filesystem>
#include <fstream>
#include <iomanip>

namespace fs = std::experimental::filesystem;

namespace sw_flat {

// Placer::run: phase 1 only. The three calls are the PL's hpwl_gradient_CU, bin_density_CU + field
// solve, and iteration_update. Meow.
void Placer::run()
{
    initialization();
    while (stop_reason == StopReason::RUNNING) {
        compute_hpwl_partials();
        compute_electric_fields();
        iteration_update();
    }
}

// Everything before iteration 1's gradient: the initial placement, iteration zero's gradient (for
// lambda_0 and the BB estimate), then step 1. Meow.
void Placer::initialization()
{
    initialize_placement();
    send_borrowed_positions();
    compute_hpwl_partials();      // performIterationZero
    compute_electric_fields();
    merge_borrowed_gradients();
    initialize_density_weight();
    ++iteration;
    update_precond_weights();
    estimate_initial_step();
    take_step();
}

// Close iteration k (its combined gradient, BB step length, metrics, schedule and stop test), then
// take step k+1. Meow.
void Placer::iteration_update()
{
    merge_borrowed_gradients();
    combine_gradients();
    step_length = lipschitz_estimate();
    record_iteration_results();
    update_schedule();
    dump_schedule_trace();
    if (check_convergence()) return;
    if (nan_detected) { stop_reason = StopReason::NAN_PARTIALS; return; }
    ++iteration;
    update_precond_weights();
    take_step();
}

// run's loop for the device checks: stops once iteration `last` has its gradient, so the positions
// and the last deposit agree. Meow.
void Placer::run_until(int last)
{
    initialization();
    while (true) {
        compute_hpwl_partials();
        compute_electric_fields();
        if (iteration >= last || stop_reason != StopReason::RUNNING) return;
        iteration_update();
    }
}

// dumpScheduleTrace: after updateSchedule, so each row pairs the schedule's inputs with the values
// the next iteration consumes. Phase is always 1, backtrack_steps always 0. Meow.
void Placer::dump_schedule_trace() const
{
    if (!cfg.dump_schedule_trace) return;
    std::ofstream f(fs::path(cfg.output_dir) / "schedule_trace.csv", std::ios_base::app);
    if (iteration == 1)
        f << "iter,hpwl,overflow,pos_norm_sq,grad_norm_sq,density_force_fraction,"
             "base_gamma,gamma,inv_gamma,step_length,nesterov_ak,momentum_coeff,density_weight,"
             "precond_coef,precond_a1_norm,precond_a2_norm,"
             "precond_kappa,phase,phase_iteration,stop_reason,backtrack_steps\n";
    f << std::scientific << std::setprecision(9)
      << iteration              << ','
      << hpwl_history.back()    << ',' << ovfw_history.back()   << ','
      << last_pos_norm_sq       << ',' << last_grad_norm_sq     << ','
      << density_force_fraction << ',' << base_gamma            << ','
      << gamma                  << ',' << inv_gamma             << ','
      << step_length            << ',' << nesterov_ak           << ','
      << momentum_coeff         << ',' << density_weight        << ','
      << precond_coef           << ',' << precond_a1_norm       << ','
      << precond_a2_norm        << ','
      << precond_kappa          << ',' << 1                     << ','
      << iteration              << ',' << (int)stop_reason      << ','
      << 0                      << '\n';
}

} // namespace sw_flat
