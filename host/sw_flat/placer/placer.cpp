// placer.cpp -- sw_only's AIEplace.cpp: the top-level loop and one iteration, with the schedule
// trace (dumpScheduleTrace) in sw_only's exact format. Meow.

#include "placer.hpp"

#include <experimental/filesystem>
#include <fstream>
#include <iomanip>

namespace fs = std::experimental::filesystem;

namespace sw_flat {

// Placer::run: phase 1 only; sw_only's NaN check follows the convergence test. Meow.
void Placer::run()
{
    initialize_placement();
    compute_hpwl_partials();      // performIterationZero
    compute_electric_fields();
    initialize_density_weight();

    while (true) {
        perform_iteration();
        if (check_convergence()) break;
        if (nan_detected) { stop_reason = StopReason::NAN_PARTIALS; break; }
    }
}

void Placer::perform_iteration()
{
    ++iteration;
    update_precond_weights();
    if (iteration == 1) estimate_initial_step();
    perform_next_step();
    record_iteration_results();
    update_schedule();

    // dumpScheduleTrace: after updateSchedule, so each row pairs the schedule's inputs with the
    // values the next iteration consumes. Phase is always 1, backtrack_steps always 0. Meow.
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
