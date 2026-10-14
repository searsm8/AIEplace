// density_map_check -- #48 phase 2's real-design check: pl/src/modules/density_map_CU.hpp against
// sw_flat_place's own deposit, on a real design part-way through placement, BIT FOR BIT.
//
//   density_map_check <config.toml> [iterations=30]
//
// Runs sw_flat_place for `iterations` with its deposit in slot order (the device's order; slot_order
// "deposit" is forced on), then hands density_map_CU the same slot positions, the footprint table and
// per-slot indices (density_inputs.hpp) and the fixed base map, and compares rho with sw_flat's
// bin_overlap * inv_bin_area, every bin. Checks, each a verdict:
//   [1] rho          bit-equal on every bin
//   [2] table        the footprint table fits FOOTPRINT_TABLE_MAX; every OWNED / FILLER slot has an entry
//   [3] mutant       one deposited slot set to NO_DEPOSIT must change rho (the comparison can fail)
// [info] how many bins the name-order deposit (sw_only's) would differ in: the order is visible. Meow.

#include "density_inputs.hpp"
#include "modules/density_map_CU.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace sw_flat;

namespace {

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::vector<float> device_rho(const Placer& placer, const DensityInputs& in) {
    const long num_beats = placer.layout.num_slots / pinrec::LANES;
    std::vector<pinrec::SlotBeat> pos_x(num_beats), pos_y(num_beats);
    std::vector<densrec::FootprintBeat> index(num_beats);
    for (long slot = 0; slot < placer.layout.num_slots; slot++) {
        pos_x[slot / pinrec::LANES].v[slot % pinrec::LANES] = placer.next.probe_pos[slot].x;
        pos_y[slot / pinrec::LANES].v[slot % pinrec::LANES] = placer.next.probe_pos[slot].y;
        index[slot / pinrec::LANES].idx[slot % pinrec::LANES] = in.index[slot];
    }
    const int grid_beats = placer.bins * placer.bins / pinrec::LANES;
    std::vector<pinrec::SlotBeat> base(grid_beats), rho(grid_beats);
    std::memcpy(base.data(), placer.fixed_base.data(), placer.fixed_base.size() * sizeof(float));
    plalgo::density_map_CU(pos_x.data(), pos_y.data(), index.data(), (int)num_beats, in.footprints.data(),
                           (int)in.footprints.size(), base.data(), placer.bins, placer.bin_w, placer.bin_h,
                           1 / placer.bin_area[0], rho.data());
    std::vector<float> out((size_t)placer.bins * placer.bins);
    std::memcpy(out.data(), rho.data(), out.size() * sizeof(float));
    return out;
}

long count_differing(const std::vector<float>& a, const std::vector<float>& b) {
    long differ = 0;
    for (size_t i = 0; i < a.size(); i++) differ += !same_bits(a[i], b[i]);
    return differ;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: density_map_check <config.toml> [iterations]\n"); return 2; }
    const int iterations = argc > 2 ? atoi(argv[2]) : 30;
    PlacerConfig cfg = read_placer_config(argv[1]);
    cfg.dump_schedule_trace = false;
    cfg.slot_order_deposit = true;
    Placer placer;
    placer.setup(cfg);
    const std::string tag = placer.design.name + " (grid " + std::to_string(placer.bins) + ")";
    if (placer.bins > densrec::GRID_MAX || placer.bins % pinrec::LANES) {
        printf("FAIL %s: grid outside density_map_CU's range (<= %d, a multiple of %d)\n", tag.c_str(), densrec::GRID_MAX, pinrec::LANES);
        return 1;
    }
    placer.initialize_placement();
    placer.compute_hpwl_partials();
    placer.compute_electric_fields();
    placer.initialize_density_weight();
    while (placer.iteration < iterations) {
        placer.perform_iteration();
        if (placer.check_convergence()) break;
    }

    // sw_flat's rho: Grid::getBinDensities' scaling of the deposit it just did. Meow.
    const float inv_bin_area = 1 / placer.bin_area[0];
    std::vector<float> golden(placer.bin_overlap.size());
    for (size_t bin = 0; bin < golden.size(); bin++) golden[bin] = placer.bin_overlap[bin] * inv_bin_area;

    const DensityInputs in = make_density_inputs(placer);
    bool ok = true;

    long deposited = 0;
    for (uint32_t idx : in.index) deposited += idx != densrec::NO_DEPOSIT;
    const bool table_ok = (int)in.footprints.size() <= densrec::FOOTPRINT_TABLE_MAX && deposited == (long)placer.movable_slots.size();
    printf("%s [2] %s table: %zu footprints (max %d), %ld of %zu movable slots deposit\n", table_ok ? "ok  " : "FAIL",
           tag.c_str(), in.footprints.size(), densrec::FOOTPRINT_TABLE_MAX, deposited, placer.movable_slots.size());
    ok &= table_ok;
    if (!table_ok) { printf("FAIL %s\n", tag.c_str()); return 1; }

    const long rho_differ = count_differing(device_rho(placer, in), golden);
    printf("%s [1] %s rho after %d iterations: %ld of %zu bins differ\n", rho_differ ? "FAIL" : "ok  ", tag.c_str(),
           placer.iteration, rho_differ, golden.size());
    ok &= rho_differ == 0;

    DensityInputs mutant = in;
    mutant.index[placer.movable_slots[0]] = densrec::NO_DEPOSIT;
    const long mutant_differ = count_differing(device_rho(placer, mutant), golden);
    printf("%s [3] %s mutant (one slot dropped): %ld bins differ\n", mutant_differ ? "ok  " : "FAIL", tag.c_str(), mutant_differ);
    ok &= mutant_differ > 0;

    // [info] sw_only's name order over the same positions. Meow.
    std::vector<float> name_order = placer.fixed_base;
    for (long slot : placer.movable_slots) {
        const Position& p = placer.next.probe_pos[slot];
        const densrec::Footprint& fp = in.footprints[in.index[slot]];
        plalgo::deposit_footprint(p.x, p.y, fp, placer.bins, placer.bin_w, placer.bin_h, name_order.data());
    }
    for (float& v : name_order) v *= inv_bin_area;
    printf("[info] %s name-order deposit differs from slot order in %ld bins\n", tag.c_str(), count_differing(name_order, golden));

    printf("%s %s\n", ok ? "PASS" : "FAIL", tag.c_str());
    return ok ? 0 : 1;
}
