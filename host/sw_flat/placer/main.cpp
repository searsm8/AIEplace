// main.cpp -- sw_flat_place: config, setup, Placer::run (placer.cpp), and the final report.
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
    const auto setup_start = std::chrono::steady_clock::now();
    placer.setup(cfg);
    const auto run_start = std::chrono::steady_clock::now();
    placer.run();
    const auto run_end = std::chrono::steady_clock::now();
    auto seconds = [](auto from, auto to) { return std::chrono::duration<double>(to - from).count(); };

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
        << "final_smoothed_overflow " << final_smoothed_overflow << '\n'
        << std::fixed << std::setprecision(3)
        << "seconds_setup " << seconds(setup_start, run_start) << '\n'
        << "seconds_run " << seconds(run_start, run_end) << '\n'
        << "seconds_gradient " << placer.time_gradient << '\n'
        << "seconds_field " << placer.time_field << '\n'
        << "seconds_combine " << placer.time_combine << '\n'
        << "seconds_metrics " << placer.time_metrics << '\n';
    printf("%s: %d iterations, %s, final HPWL %.6e (exact %.6e), overflow %.4f\n", placer.design.name.c_str(),
           placer.iteration, sw_flat::stop_reason_name(placer.stop_reason), final_hpwl, final_hpwl_exact, final_overflow);
    return 0;
}
