// bin_density_check -- #48 phase 2's real-design check: pl/src/modules/bin_density_CU.hpp against
// sw_flat_place's own fixed-point deposit, on a real design part-way through placement, BIT FOR BIT.
//
//   bin_density_check <config.toml> [iterations=30]
//
// Runs sw_flat_place for `iterations` with bin_density_CU's arithmetic on ([sw_flat] deposit = "fixed",
// 18.18) and #48 B' (overflow_map = "snapshot", fixed_base = "exact"), then hands bin_density_CU the same
// slot positions, footprint table, per-slot indices, fixed base and scalars (density_inputs.hpp).
// Checks, each a verdict:
//   [1] rho          bit-equal to sw_flat's (float)acc * RHO_UNIT on every bin
//   [2] table        the footprint table fits FOOTPRINT_TABLE_MAX; every OWNED / FILLER slot has an entry
//   [3] mutant       one deposited slot set to NO_DEPOSIT must change rho (the comparison can fail)
//   [4] overflow     the filler tail starts on a beat boundary; the CU's excess equals sw_flat's
//                    snapshot_excess, and the overflow formed from it equals snapshot_overflow, bit for bit
//   [5] order        the CU fed the slots shuffled (cells among cells, fillers among fillers): rho and the
//                    excess bit-identical to [1] / [4], so any lane count or banking is safe
//   [6] word         the densest bin fits RHO_WORD_BITS (sw_flat would also have exited)
// Meow.

#include "density_inputs.hpp"
#include "modules/bin_density_CU.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>

using namespace sw_flat;

namespace {

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

struct DeviceOut { std::vector<float> rho; uint64_t excess = 0; };

// `order[i]` is the slot that goes to stream position i; identity is the device's own order. Meow.
DeviceOut run_device(const Placer& placer, const DensityInputs& in, const std::vector<long>& order) {
    const long num_beats = placer.layout.num_slots / pinrec::LANES;
    std::vector<pinrec::SlotBeat> pos_x(num_beats), pos_y(num_beats);
    std::vector<densrec::FootprintBeat> index(num_beats);
    for (long i = 0; i < placer.layout.num_slots; i++) {
        const long slot = order[i];
        pos_x[i / pinrec::LANES].v[i % pinrec::LANES] = placer.next.probe_pos[slot].x;
        pos_y[i / pinrec::LANES].v[i % pinrec::LANES] = placer.next.probe_pos[slot].y;
        index[i / pinrec::LANES].idx[i % pinrec::LANES] = in.index[slot];
    }
    const int grid_beats = placer.bins * placer.bins / pinrec::LANES;
    std::vector<pinrec::SlotBeat> rho(grid_beats);
    DeviceOut out;
    plalgo::bin_density_CU(pos_x.data(), pos_y.data(), index.data(), (int)num_beats,
                           (int)(placer.layout.first_filler_slot / pinrec::LANES), in.footprints.data(),
                           (int)in.footprints.size(), in.fixed_base.data(), placer.bins, placer.bin_w, placer.bin_h,
                           in.fixed_scale, in.overflow_cap, rho.data(), out.excess);
    out.rho.resize((size_t)placer.bins * placer.bins);
    std::memcpy(out.rho.data(), rho.data(), out.rho.size() * sizeof(float));
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
    if (argc < 2) { fprintf(stderr, "usage: bin_density_check <config.toml> [iterations]\n"); return 2; }
    const int iterations = argc > 2 ? atoi(argv[2]) : 30;
    PlacerConfig cfg = read_placer_config(argv[1]);
    cfg.dump_schedule_trace = false;
    cfg.slot_order_deposit = true;
    cfg.overflow_snapshot = true;
    cfg.fixed_base_exact = true;
    cfg.deposit_fixed = true;
    Placer placer;
    placer.setup(cfg);
    const std::string tag = placer.design.name + " (grid " + std::to_string(placer.bins) + ")";
    if (placer.bins > densrec::GRID_MAX || placer.bins % pinrec::LANES) {
        printf("FAIL %s: grid outside bin_density_CU's range (<= %d, a multiple of %d)\n", tag.c_str(), densrec::GRID_MAX, pinrec::LANES);
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

    // sw_flat's rho: deposit_fixed_point's drain of the deposit it just did. Meow.
    std::vector<float> golden(placer.bin_fixed.size());
    uint64_t golden_peak = 0;
    for (size_t bin = 0; bin < golden.size(); bin++) {
        golden[bin] = (float)placer.bin_fixed[bin] * densrec::RHO_UNIT;
        golden_peak = std::max(golden_peak, placer.bin_fixed[bin]);
    }

    const DensityInputs in = make_density_inputs(placer);
    bool ok = true;

    long deposited = 0;
    for (uint32_t idx : in.index) deposited += idx != densrec::NO_DEPOSIT;
    const bool table_ok = (int)in.footprints.size() <= densrec::FOOTPRINT_TABLE_MAX && deposited == (long)placer.movable_slots.size();
    printf("%s [2] %s table: %zu footprints (max %d), %ld of %zu movable slots deposit\n", table_ok ? "ok  " : "FAIL",
           tag.c_str(), in.footprints.size(), densrec::FOOTPRINT_TABLE_MAX, deposited, placer.movable_slots.size());
    ok &= table_ok;
    if (!table_ok) { printf("FAIL %s\n", tag.c_str()); return 1; }

    std::vector<long> identity(placer.layout.num_slots);
    std::iota(identity.begin(), identity.end(), 0L);
    const DeviceOut device = run_device(placer, in, identity);
    const long rho_differ = count_differing(device.rho, golden);
    printf("%s [1] %s rho after %d iterations: %ld of %zu bins differ\n", rho_differ ? "FAIL" : "ok  ", tag.c_str(),
           placer.iteration, rho_differ, golden.size());
    ok &= rho_differ == 0;

    DensityInputs mutant = in;
    mutant.index[placer.movable_slots[0]] = densrec::NO_DEPOSIT;
    const long mutant_differ = count_differing(run_device(placer, mutant, identity).rho, golden);
    printf("%s [3] %s mutant (one slot dropped): %ld bins differ\n", mutant_differ ? "ok  " : "FAIL", tag.c_str(), mutant_differ);
    ok &= mutant_differ > 0;

    const bool aligned = placer.layout.first_filler_slot % pinrec::LANES == 0;
    const float device_overflow = placer.overflow_from_excess(device.excess);
    const bool overflow_ok = aligned && device.excess == placer.snapshot_excess && same_bits(device_overflow, placer.snapshot_overflow);
    printf("%s [4] %s overflow: excess CU %llu, sw_flat %llu; overflow CU %.9g, sw_flat %.9g%s\n", overflow_ok ? "ok  " : "FAIL",
           tag.c_str(), (unsigned long long)device.excess, (unsigned long long)placer.snapshot_excess, device_overflow,
           placer.snapshot_overflow, aligned ? "" : ", filler tail not beat-aligned");
    ok &= overflow_ok;

    std::vector<long> shuffled = identity;
    std::mt19937 rng(11);
    std::shuffle(shuffled.begin(), shuffled.begin() + placer.layout.first_filler_slot, rng);
    std::shuffle(shuffled.begin() + placer.layout.first_filler_slot, shuffled.end(), rng);
    const DeviceOut reordered = run_device(placer, in, shuffled);
    const long order_differ = count_differing(reordered.rho, golden);
    const bool order_ok = order_differ == 0 && reordered.excess == device.excess;
    printf("%s [5] %s shuffled slot order: %ld bins differ, excess %s\n", order_ok ? "ok  " : "FAIL", tag.c_str(), order_differ,
           reordered.excess == device.excess ? "equal" : "DIFFERS");
    ok &= order_ok;

    const bool word_ok = (golden_peak >> densrec::RHO_WORD_BITS) == 0;
    printf("%s [6] %s densest bin %.6g bin areas (word holds < %g)\n", word_ok ? "ok  " : "FAIL", tag.c_str(),
           (double)golden_peak * densrec::RHO_UNIT, std::ldexp(1.0, densrec::RHO_WORD_BITS - densrec::RHO_FRAC_BITS));
    ok &= word_ok;

    printf("%s %s\n", ok ? "PASS" : "FAIL", tag.c_str());
    return ok ? 0 : 1;
}
