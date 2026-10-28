// gradient_bench -- what the PL's exp table costs or saves in SOFTWARE: Placer::compute_hpwl_partials
// with std::exp vs the table (wa_exp = "lut"), on identical positions, the same design and threads.
//
//   gradient_bench <config.toml> [reps=20]
//
// Positions: the initial placement plus 50 Nesterov steps, so nets have real spans. Prints ms per
// call for each mode and the LUT gradient's error vs exact ([info]: a measurement, not a verdict;
// the PL's own accuracy is held by pl/test). Meow.

#include "placer/placer.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <omp.h>

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: gradient_bench <config.toml> [reps]\n"); return 2; }
    const int reps = argc > 2 ? atoi(argv[2]) : 20;
    sw_flat::PlacerConfig cfg = sw_flat::read_placer_config(argv[1]);
    cfg.dump_schedule_trace = false;
    sw_flat::Placer placer;
    placer.setup(cfg);
    placer.run_until(50);

    auto time_mode = [&](bool lut, std::vector<sw_flat::Gradient>& grad) {
        placer.cfg.wa_exp_lut = lut;
        placer.compute_hpwl_partials();   // warm
        const auto start = std::chrono::steady_clock::now();
        for (int r = 0; r < reps; r++) placer.compute_hpwl_partials();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / reps;
        grad = placer.next.probe_grad;
        return ms;
    };
    std::vector<sw_flat::Gradient> exact, lut;
    const double exact_ms = time_mode(false, exact), lut_ms = time_mode(true, lut);

    double err2 = 0, ref2 = 0;
    for (int i = 0; i < placer.filler_start; i++) {
        const long slot = placer.movable_slots[i];
        for (int axis = 0; axis < 2; axis++) {
            const double e = axis ? exact[slot].y : exact[slot].x, l = axis ? lut[slot].y : lut[slot].x;
            err2 += (l - e) * (l - e);
            ref2 += e * e;
        }
    }
    const double rel_rms = std::sqrt(err2 / ref2);
    printf("%s: %d threads, %zu pins, %zu gradient nets | exact %.2f ms  lut %.2f ms  (lut/exact %.3f) | [info] lut rel_rms vs exact %.2e\n",
           placer.design.name.c_str(), omp_get_max_threads(), placer.design.pin_node.size(), placer.gradient_nets.size(),
           exact_ms, lut_ms, lut_ms / exact_ms, rel_rms);
    return 0;
}
