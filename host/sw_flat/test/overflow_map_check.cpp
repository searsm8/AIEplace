// overflow_map_check -- #48 option B': the schedule's overflow reduced from the solver's own map (fixed
// base + cells, before the fillers; overflow_map = "snapshot"), against sw_only's separate metric
// (compute_overflow(true)), on one placement part-way through a run.
//
//   overflow_map_check <config.toml> [iterations=30]
//
// Runs sw_flat_place (default config) for `iterations`, then on the same positions computes:
//   separate   sw_only's metric: its own deposit, exact-size fixed base, no fillers
//   exact      the snapshot over an exact-size fixed base (B', XPlace's construction)
//   clamped    the snapshot over sw_only's clamped fixed base (B)
// Checks, each a verdict:
//   [1] B' == separate up to rounding: |exact / separate - 1| <= B_PRIME_REL_MAX
//   [2] the snapshot is taken before the fillers: reducing the full map (fillers in) cannot read lower
// [info] B's bias (clamped vs separate), the fixed nodes the clamp stretches, and the bins it moves. Meow.

#include "placer/placer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace sw_flat;

namespace {

// Observed (2026-10-14, 44 designs): 0 on 38; up to 2.5e-4 at the stop on 6 ISPD2015 designs, whose
// bin width is not dyadic, so the two overlap forms round apart and d - cap cancels as the overflow
// falls. B (the clamped base) reads 1.3e-2..6.7e-2 low at the stop on bigblue2/3/4. Meow.
constexpr double B_PRIME_REL_MAX = 1e-3;

float snapshot(Placer& placer, bool exact_base)
{
    placer.cfg.overflow_snapshot = true;
    placer.cfg.fixed_base_exact = exact_base;
    placer.deposit_fixed_base();
    placer.compute_electric_fields();
    return placer.snapshot_overflow;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: overflow_map_check <config.toml> [iterations]\n"); return 2; }
    const int iterations = argc > 2 ? atoi(argv[2]) : 30;
    PlacerConfig cfg = read_placer_config(argv[1]);
    cfg.dump_schedule_trace = false;
    Placer placer;
    placer.setup(cfg);
    const std::string tag = placer.design.name + " (grid " + std::to_string(placer.bins) + ")";
    placer.initialize_placement();
    placer.compute_hpwl_partials();
    placer.compute_electric_fields();
    placer.initialize_density_weight();
    while (placer.iteration < iterations) {
        placer.perform_iteration();
        if (placer.check_convergence()) break;
    }

    const float separate = placer.compute_overflow(true);
    const float clamped = snapshot(placer, false);
    const std::vector<float> clamped_base = placer.fixed_base;
    const float exact = snapshot(placer, true);

    const float cap = placer.bin_w * placer.bin_h * placer.cfg.target_density;
    float full_area = 0.0f;
    for (float d : placer.bin_overlap) full_area += std::max(0.0f, d - cap);
    const float with_fillers = full_area / (placer.total_movable_area + 1e-8f);

    bool ok = true;
    const double exact_rel = (double)exact / separate - 1;
    const bool exact_ok = std::fabs(exact_rel) <= B_PRIME_REL_MAX;
    printf("%s [1] %s iteration %d: separate %.6e, B' snapshot %.6e, rel %+.3e (max %.0e)\n", exact_ok ? "ok  " : "FAIL",
           tag.c_str(), placer.iteration, separate, exact, exact_rel, B_PRIME_REL_MAX);
    ok &= exact_ok;

    // Not strictly higher: early on the fillers can all sit in bins below the cap. Meow.
    const bool order_ok = with_fillers >= exact;
    printf("%s [2] %s with the fillers the map reads %.6e >= %.6e\n", order_ok ? "ok  " : "FAIL", tag.c_str(), with_fillers, exact);
    ok &= order_ok;

    // A fixed node the clamp stretches: nonzero area, either side under sqrt(2) bins. Meow.
    long stretched = 0;
    for (int node : placer.fixed_nodes) {
        const float w = placer.design.node_width[node], h = placer.design.node_height[node];
        stretched += w > 0.0f && h > 0.0f && (w < placer.bin_w * (float)M_SQRT2 || h < placer.bin_h * (float)M_SQRT2);
    }
    long bins_moved = 0;
    for (size_t bin = 0; bin < clamped_base.size(); bin++) bins_moved += clamped_base[bin] != placer.fixed_base[bin];
    printf("[info] %s B (clamped base) rel %+.3e; %ld of %zu fixed nodes stretched; %ld bins of the base differ\n",
           tag.c_str(), (double)clamped / separate - 1, stretched, placer.fixed_nodes.size(), bins_moved);

    printf("%s %s\n", ok ? "PASS" : "FAIL", tag.c_str());
    return ok ? 0 : 1;
}
