// main.cpp -- sw_flat_place: sw_only's AIEplace.cpp loop, its schedule trace, and the final report.
//
//   sw_flat_place <config.toml> <output_dir>
//
// Writes <output_dir>/schedule_trace.csv (when [output] dump_schedule_trace = true) in sw_only's exact
// format, so the two runs compare as text, and <output_dir>/final.txt. Meow.

#include "placer.hpp"

#include <cstdio>
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

int main(int argc, char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: sw_flat_place <config.toml> <output_dir>\n");
        return 2;
    }
    sw_flat::PlacerConfig cfg = sw_flat::read_placer_config(argv[1]);
    cfg.output_dir = argv[2];
    fs::create_directories(cfg.output_dir);
    fs::remove(fs::path(cfg.output_dir) / "schedule_trace.csv");

    sw_flat::Placer placer;
    placer.setup(cfg);
    placer.run();

    // printFinalResults: restore the chosen tracker into u and v, then measure it. Meow.
    const int chosen = placer.select_best_solution();
    if (chosen >= 0)
        for (long slot : placer.movable_slots) {
            placer.next.node_pos[slot] = placer.best_pos[chosen][slot];
            placer.next.probe_pos[slot] = placer.best_pos[chosen][slot];
        }
    const float final_hpwl = placer.total_wirelength(placer.cfg.ignore_net_degree, false);
    const float final_hpwl_exact = placer.total_wirelength(1000000000, false);
    const float final_overflow = placer.compute_overflow(false);
    const float final_smoothed_overflow = placer.compute_overflow(true);

    std::ofstream out(fs::path(cfg.output_dir) / "final.txt");
    static const char* SLOT_NAMES[] = {"primary", "aux", "rollback"};
    out << std::scientific << std::setprecision(9)
        << "design " << placer.design.name << '\n'
        << "grid " << placer.bins << '\n'
        << "slots " << placer.layout.num_slots << " chunks " << placer.layout.num_chunks
        << " fillers " << placer.fillers.count << '\n'
        << "iterations " << placer.iteration << '\n'
        << "stop_reason " << sw_flat::stop_reason_name(placer.stop_reason) << '\n'
        << "best " << (chosen >= 0 ? SLOT_NAMES[chosen] : "none");
    if (chosen >= 0)
        out << " iteration " << placer.best[chosen].iteration << " hpwl " << placer.best[chosen].hpwl
            << " overflow " << placer.best[chosen].overflow;
    out << '\n'
        << "final_hpwl " << final_hpwl << '\n'
        << "final_hpwl_exact " << final_hpwl_exact << '\n'
        << "final_overflow " << final_overflow << '\n'
        << "final_smoothed_overflow " << final_smoothed_overflow << '\n';
    printf("%s: %d iterations, %s, final HPWL %.6e (exact %.6e), overflow %.4f\n", placer.design.name.c_str(),
           placer.iteration, sw_flat::stop_reason_name(placer.stop_reason), final_hpwl, final_hpwl_exact, final_overflow);
    return 0;
}
