// Verify density_map_CU (src/modules/density_map_CU.hpp) -- the bin density map from slot-ordered
// positions, footprint indices and a fixed-node base map.
//
// Golden: a naive double-precision deposit written here from sw_flat density.cpp (node_footprint,
// for_each_overlap), on a synthetic slot range: a cell library, movable macros at td < 1 (the weight
// override), cells hanging off the die edge, one shared filler size, and NO_DEPOSIT slots (tail
// copies, pins, padding) whose positions are NaN, so a wrongly read slot poisons the map.
//
//   [1] MATCH     rho vs golden, rel_rms < REL_RMS_MAX and worst-bin error / peak < PEAK_REL_MAX, at
//                 128 and 1024 bins. Not a per-bin max_rel: a float corner at x ~ 2000 is off by ~1e-4,
//                 a real 2% of a 1024-grid bin that the footprint only grazes.
//   [2] SKIP      every bin finite: no NO_DEPOSIT slot was read
//   [3] MASS      total deposited area == golden's, rel < MASS_REL_MAX
//   [4] COVERAGE  a macro spans >= 8x8 bins; some cell is clipped by the die edge
//   [5] OVERFLOW  the overflow (fixed base + cells, before the fillers; #48 B') vs golden, rel <
//                 OVERFLOW_REL_MAX, and farther than that from the with-fillers overflow, so the
//                 check can tell whether the CU reduced at the filler barrier
//
// Real designs vs sw_flat Placer::bin_overlap are the next harness step, not this one. Meow.

#include "modules/density_map_CU.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace plalgo;

constexpr double REL_RMS_MAX  = 1e-5;   // observed 2.9e-6 (float vs double golden), 2026-10-13
constexpr double PEAK_REL_MAX = 3e-5;   // worst bin error / peak bin density; observed 8.3e-6 at 1024
constexpr double MASS_REL_MAX = 1e-5;
constexpr double OVERFLOW_REL_MAX = 2e-5;  // observed 2.6e-6 at 1024 (float, tree order, vs double), 2026-10-15. Meow.

struct SyntheticDesign {
    float die = 2000.0f, target_density = 0.8f;
    std::vector<float> x, y;                // per slot; NaN on NO_DEPOSIT
    std::vector<float> w, h;                // per slot; 0 on NO_DEPOSIT
    std::vector<char>  is_macro, deposits;
    std::vector<float> fixed_x, fixed_y, fixed_w, fixed_h;
    long first_filler_slot = 0;
    long num_slots() const { return (long)x.size(); }
};

static SyntheticDesign make_design(unsigned seed) {
    SyntheticDesign d;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float row_h = 12.0f;
    auto add = [&](float w, float h, bool macro, bool deposits) {
        d.w.push_back(deposits ? w : 0.0f);
        d.h.push_back(deposits ? h : 0.0f);
        d.is_macro.push_back(macro);
        d.deposits.push_back(deposits);
        d.x.push_back(deposits ? -0.5f * w + unit(rng) * d.die : NAN);   // edge overhang on purpose
        d.y.push_back(deposits ? -0.5f * h + unit(rng) * d.die : NAN);
    };
    const float library_w[] = {2, 3, 4, 6, 8, 10, 14, 20};
    for (int i = 0; i < 20000; i++) {
        if (i % 7 == 3) add(0, 0, false, false);   // tail copy / pin / padding
        else            add(library_w[i % 8], row_h, false, true);
    }
    for (int i = 0; i < 6; i++) add(150.0f + 40 * i, 120.0f + 30 * i, true, true);
    while (d.num_slots() % pinrec::LANES) add(0, 0, false, false);   // the packer's chunks end on a beat
    d.first_filler_slot = d.num_slots();
    for (int i = 0; i < 4000; i++) add(9.0f, row_h, false, true);    // fillers: one shared size
    while (d.num_slots() % pinrec::LANES) add(0, 0, false, false);   // row padding
    for (int i = 0; i < 5; i++) {
        d.fixed_w.push_back(200.0f); d.fixed_h.push_back(180.0f);
        d.fixed_x.push_back(unit(rng) * (d.die - 200.0f)); d.fixed_y.push_back(unit(rng) * (d.die - 180.0f));
    }
    return d;
}

// sw_flat density.cpp node_footprint + for_each_overlap, in double. Meow.
static void golden_deposit(double x, double y, double w, double h, bool macro, double td, int bins,
                           double bin_w, double bin_h, std::vector<double>& overlap) {
    const double cw = std::max(w, bin_w * M_SQRT2), ch = std::max(h, bin_h * M_SQRT2);
    const double weight = (macro && td < 1.0) ? td : (w * h) / (cw * ch);
    const double xl = x + 0.5 * w - 0.5 * cw, yl = y + 0.5 * h - 0.5 * ch, xh = xl + cw, yh = yl + ch;
    const int col_lo = std::max(0, (int)(xl / bin_w)), col_hi = std::min(bins - 1, (int)(xh / bin_w));
    const int row_lo = std::max(0, (int)(yl / bin_h)), row_hi = std::min(bins - 1, (int)(yh / bin_h));
    for (int col = col_lo; col <= col_hi; col++) {
        const double ow = std::min(xh, (col + 1) * bin_w) - std::max(xl, col * bin_w);
        if (ow <= 0) continue;
        for (int row = row_lo; row <= row_hi; row++) {
            const double oh = std::min(yh, (row + 1) * bin_h) - std::max(yl, row * bin_h);
            if (oh > 0) overlap[(size_t)col * bins + row] += ow * oh * weight;
        }
    }
}

static bool run(const SyntheticDesign& d, int bins) {
    const float bin_w = d.die / bins, bin_h = d.die / bins, bin_area = bin_w * bin_h;
    const size_t num_bins = (size_t)bins * bins;
    const long num_slots = d.num_slots();

    // Host side: fixed base map (deposited, then capped), footprint table, per-slot indices.
    std::vector<double> golden(num_bins, 0.0);
    for (size_t i = 0; i < d.fixed_x.size(); i++)
        golden_deposit(d.fixed_x[i], d.fixed_y[i], d.fixed_w[i], d.fixed_h[i], false, d.target_density, bins, bin_w, bin_h, golden);
    for (double& v : golden) v = std::min(v, (double)bin_area * d.target_density);
    std::vector<pinrec::SlotBeat> fixed_base(num_bins / pinrec::LANES);
    for (size_t bin = 0; bin < num_bins; bin++) fixed_base[bin / pinrec::LANES].v[bin % pinrec::LANES] = (float)golden[bin];

    // XPlace's overflow_fn on the map between cells and fillers, then again with the fillers. Meow.
    const double cap = (double)bin_w * bin_h * d.target_density;
    double movable_area = 0;
    for (long s = 0; s < d.first_filler_slot; s++) movable_area += (double)d.w[s] * d.h[s];
    auto overflow_of = [&](const std::vector<double>& overlap) {
        double area = 0;
        for (double v : overlap) area += std::max(0.0, v - cap);
        return area / movable_area;
    };
    double golden_overflow = 0;

    std::vector<densrec::Footprint> table;
    std::vector<pinrec::SlotBeat> pos_x(num_slots / pinrec::LANES), pos_y(num_slots / pinrec::LANES);
    std::vector<densrec::FootprintBeat> idx(num_slots / pinrec::LANES);
    for (long s = 0; s < num_slots; s++) {
        if (s == d.first_filler_slot) golden_overflow = overflow_of(golden);
        pos_x[s / pinrec::LANES].v[s % pinrec::LANES] = d.x[s];
        pos_y[s / pinrec::LANES].v[s % pinrec::LANES] = d.y[s];
        uint32_t entry = densrec::NO_DEPOSIT;
        if (d.deposits[s]) {
            const densrec::Footprint fp = densrec::make_footprint(d.w[s], d.h[s], d.is_macro[s], bin_w, bin_h, d.target_density, true);
            entry = 0;
            while (entry < table.size() && std::memcmp(&table[entry], &fp, sizeof fp)) entry++;
            if (entry == table.size()) table.push_back(fp);
            golden_deposit(d.x[s], d.y[s], d.w[s], d.h[s], d.is_macro[s], d.target_density, bins, bin_w, bin_h, golden);
        }
        idx[s / pinrec::LANES].idx[s % pinrec::LANES] = entry;
    }
    if ((int)table.size() > densrec::FOOTPRINT_TABLE_MAX) { printf("  table %zu > max\n", table.size()); return false; }

    std::vector<pinrec::SlotBeat> rho(num_bins / pinrec::LANES);
    float overflow = 0;
    density_map_CU(pos_x.data(), pos_y.data(), idx.data(), (int)(num_slots / pinrec::LANES),
                   (int)(d.first_filler_slot / pinrec::LANES), table.data(), (int)table.size(), fixed_base.data(),
                   bins, bin_w, bin_h, 1.0f / bin_area, bin_w * bin_h * d.target_density,
                   (float)movable_area + 1e-8f, rho.data(), overflow);
    const double overflow_rel = std::fabs(overflow - golden_overflow) / golden_overflow;
    const double filler_rel = std::fabs(overflow_of(golden) - golden_overflow) / golden_overflow;
    const bool overflow_ok = overflow_rel < OVERFLOW_REL_MAX && filler_rel > OVERFLOW_REL_MAX;

    double err_sq = 0, ref_sq = 0, max_err = 0, peak = 0, mass = 0, golden_mass = 0;
    bool finite = true;
    for (size_t bin = 0; bin < num_bins; bin++) {
        const double device = rho[bin / pinrec::LANES].v[bin % pinrec::LANES];
        const double expect = golden[bin] / bin_area;
        finite &= std::isfinite(device);
        err_sq += (device - expect) * (device - expect);
        ref_sq += expect * expect;
        max_err = std::max(max_err, std::fabs(device - expect));
        peak = std::max(peak, expect);
        mass += device * bin_area;
        golden_mass += golden[bin];
    }
    const double rel_rms = std::sqrt(err_sq / ref_sq), peak_rel = max_err / peak, mass_rel = std::fabs(mass - golden_mass) / golden_mass;

    const float big_macro = d.w[20000 + 5];   // the largest macro
    const bool coverage = big_macro / bin_w >= 8.0f;
    bool clipped = false;
    for (long s = 0; s < num_slots && !clipped; s++) clipped = d.deposits[s] && (d.x[s] < 0 || d.y[s] < 0);

    const bool pass = finite && rel_rms < REL_RMS_MAX && peak_rel < PEAK_REL_MAX && mass_rel < MASS_REL_MAX && coverage && clipped && overflow_ok;
    printf("  bins=%4d footprints=%zu  rel_rms=%.3g (<%g)  peak_rel=%.3g (<%g)  mass_rel=%.3g (<%g)  finite=%d  macro_bins=%.1f  clipped=%d\n"
           "            overflow=%.6g golden=%.6g rel=%.3g (<%g), with fillers rel=%.3g (>%g)  %s\n",
           bins, table.size(), rel_rms, REL_RMS_MAX, peak_rel, PEAK_REL_MAX, mass_rel, MASS_REL_MAX, finite,
           big_macro / bin_w, clipped, overflow, golden_overflow, overflow_rel, OVERFLOW_REL_MAX, filler_rel,
           OVERFLOW_REL_MAX, pass ? "PASS" : "FAIL");
    return pass;
}

int main() {
    const SyntheticDesign d = make_design(7);
    bool pass = true;
    for (int bins : {128, 1024}) pass &= run(d, bins);
    printf(pass ? "density_map_CU_test: PASS\n" : "density_map_CU_test: FAIL\n");
    return pass ? 0 : 1;
}
