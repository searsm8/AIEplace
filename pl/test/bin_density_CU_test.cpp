// Verify bin_density_CU (src/modules/bin_density_CU.hpp) -- the bin density map from slot-ordered
// positions, footprint indices and a fixed-node base map, accumulated in fixed point (#48) with F from
// the total-area bound, over 16 banks (4 x 4 interleaved) with forwarding; wide footprints take the slow path.
//
// Golden: a naive double-precision deposit written here from sw_flat density.cpp (node_footprint,
// for_each_overlap), on a synthetic slot range: a cell library, movable macros at td < 1 (the weight
// override), cells hanging off the die edge, one shared filler size, and NO_DEPOSIT slots (tail
// copies, pins, padding) whose positions are NaN, so a wrongly read slot poisons the map.
//
//   [1] MATCH     rho vs golden, rel_rms < REL_RMS_MAX and worst-bin error / peak < PEAK_REL_MAX, at
//                 128 and 1024 bins. The error is the fixed point's: each overlap rounds to 2^-19 of a bin.
//   [2] SKIP      every bin finite: no NO_DEPOSIT slot was read
//   [3] MASS      total deposited area == golden's, rel < MASS_REL_MAX
//   [4] COVERAGE  a macro spans >= 8x8 bins; some cell is clipped by the die edge
//   [5] OVERFLOW  the overflow formed from the CU's integer excess (fixed base + cells, before the
//                 fillers; #48 B') vs golden, rel < OVERFLOW_REL_MAX, and farther than that from the
//                 with-fillers overflow, so the check can tell whether the CU reduced at the filler barrier
//   [6] ORDER     the same slots shuffled (cells among cells, fillers among fillers): rho and the excess
//                 BIT-IDENTICAL. The point of the integer accumulator. Negative control: a float
//                 accumulator over the same two orders must differ in some bin, or the shuffle proved nothing.
//   [7] WORD      the densest bin fits URAM_WORD_WIDTH (off-synthesis the accumulator is wider, so a
//                 bin the URAM word would wrap shows up here)
//   [8] PATHS     both the fast (banked) and the slow path deposit something, cells and fillers alike
// C simulation runs the pipeline sequentially, so it cannot test the forwarding; RTL co-simulation
// does (pl/synth/bin_density_CU/cosim).
//
// Real designs vs sw_flat are host/sw_flat's `make check-bin-density` (bit-exact). Meow.

#include "modules/bin_density_CU.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

using namespace plalgo;

// vs the double golden, per grid. Past F ~ 20 the error stops falling with F: what is left is the float
// geometry, which sw_flat shares (so the real-design check is bit-exact, not this). 128 bins, die 2000:
// floor rho rel_rms 2.8e-6, overflow ~2e-6 (F = 14 would give 6.3e-5 / 1.4e-5). 1024 bins, die 16000
// (float ulp 1/1024 there): rho 3.55e-5, peak 4.9e-5, mass 1.3e-5, overflow 1.0e-5, the same at F = 18..30.
// Bounds keep ~3x over the observed values at the F each grid gets. Meow.
struct Bounds { double rel_rms, peak_rel, mass_rel, overflow_rel; };
constexpr Bounds BOUNDS_128  = {1.5e-5, 3e-5, 5e-6, 1e-5};
constexpr Bounds BOUNDS_1024 = {1e-4, 1.5e-4, 4e-5, 3e-5};

struct SyntheticDesign {
    float die = 2000.0f, target_density = 0.8f;
    std::vector<float> x, y;                // per slot; NaN on NO_DEPOSIT
    std::vector<float> w, h;                // per slot; 0 on NO_DEPOSIT
    std::vector<char>  is_macro, deposits;
    std::vector<float> fixed_x, fixed_y, fixed_w, fixed_h;
    long first_filler_slot = 0;
    long num_slots() const { return (long)x.size(); }
};

// die scales with the grid, so a 12-unit row is about one bin tall at 1024 as on real designs (and the
// fast path is exercised); the macros still span 10+ bins. Meow.
static SyntheticDesign make_design(unsigned seed, float die) {
    SyntheticDesign d;
    d.die = die;
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

// The same slots in another order: cells permuted among the cell range, fillers among the filler tail. Meow.
static SyntheticDesign shuffled(const SyntheticDesign& d, unsigned seed) {
    std::vector<long> order(d.num_slots());
    std::iota(order.begin(), order.end(), 0L);
    std::mt19937 rng(seed);
    std::shuffle(order.begin(), order.begin() + d.first_filler_slot, rng);
    std::shuffle(order.begin() + d.first_filler_slot, order.end(), rng);
    SyntheticDesign s = d;
    for (long i = 0; i < d.num_slots(); i++) {
        const long from = order[i];
        s.x[i] = d.x[from]; s.y[i] = d.y[from]; s.w[i] = d.w[from]; s.h[i] = d.h[from];
        s.is_macro[i] = d.is_macro[from]; s.deposits[i] = d.deposits[from];
    }
    return s;
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

// The negative control for [6]: the same deposit with a float accumulator, in slot order. Meow.
static std::vector<float> float_deposit(const SyntheticDesign& d, int bins) {
    const float bin_w = d.die / bins, bin_h = d.die / bins;
    std::vector<float> map((size_t)bins * bins, 0.0f);
    for (long s = 0; s < d.num_slots(); s++) {
        if (!d.deposits[s]) continue;
        const densrec::Footprint fp = densrec::make_footprint(d.w[s], d.h[s], d.is_macro[s], bin_w, bin_h, d.target_density, true);
        const float xl = d.x[s] + fp.half_w - fp.half_cw, yl = d.y[s] + fp.half_h - fp.half_ch;
        const float xh = xl + 2.0f * fp.half_cw, yh = yl + 2.0f * fp.half_ch;
        for (int col = std::max(0, (int)(xl / bin_w)); col <= std::min(bins - 1, (int)(xh / bin_w)); col++) {
            const float ow = std::min(xh, col * bin_w + bin_w) - std::max(xl, col * bin_w);
            if (ow <= 0.0f) continue;
            for (int row = std::max(0, (int)(yl / bin_h)); row <= std::min(bins - 1, (int)(yh / bin_h)); row++) {
                const float oh = std::min(yh, row * bin_h + bin_h) - std::max(yl, row * bin_h);
                if (oh > 0.0f) map[(size_t)col * bins + row] += ow * oh * fp.weight;
            }
        }
    }
    return map;
}

struct DeviceRun { std::vector<pinrec::SlotBeat> rho; uint64_t excess = 0; int frac_bits = 0; long fast = 0, slow = 0, slow_cells = 0; };

// Host side (base map in fixed units, footprint table, per-slot indices), then the CU. Meow.
static DeviceRun run_device(const SyntheticDesign& d, int bins, const std::vector<double>& base_area) {
    const float bin_w = d.die / bins, bin_h = d.die / bins, bin_area = bin_w * bin_h;
    const size_t num_bins = (size_t)bins * bins;
    const long num_slots = d.num_slots();

    // The total-area bound (sw_flat Placer::deposit_fixed_base): base + every footprint's whole area. Meow.
    double total_area = 0;
    for (double area : base_area) total_area += area;
    for (long s = 0; s < num_slots; s++)
        if (d.deposits[s]) {
            const densrec::Footprint fp = densrec::make_footprint(d.w[s], d.h[s], d.is_macro[s], bin_w, bin_h, d.target_density, true);
            total_area += (double)fp.weight * (2.0f * fp.half_cw) * (2.0f * fp.half_ch);
        }
    DeviceRun out;
    out.frac_bits = densrec::rho_frac_bits(total_area / bin_area);
    const float fixed_scale = std::ldexp(1.0f, out.frac_bits) / bin_area;
    std::vector<densrec::FixedBeat> fixed_base(num_bins / pinrec::LANES);
    for (size_t bin = 0; bin < num_bins; bin++)
        fixed_base[bin / pinrec::LANES].q[bin % pinrec::LANES] = densrec::to_fixed((float)base_area[bin] * fixed_scale);

    std::vector<densrec::Footprint> table;
    std::vector<pinrec::SlotBeat> pos_x(num_slots / pinrec::LANES), pos_y(num_slots / pinrec::LANES);
    std::vector<densrec::FootprintBeat> idx(num_slots / pinrec::LANES);
    std::vector<densrec::SlowSlot> slow;
    int num_slow_cells = 0;
    for (long s = 0; s < num_slots; s++) {
        pos_x[s / pinrec::LANES].v[s % pinrec::LANES] = d.x[s];
        pos_y[s / pinrec::LANES].v[s % pinrec::LANES] = d.y[s];
        uint32_t entry = densrec::NO_DEPOSIT;
        if (d.deposits[s]) {
            const densrec::Footprint fp = densrec::make_footprint(d.w[s], d.h[s], d.is_macro[s], bin_w, bin_h, d.target_density, true);
            entry = 0;
            while (entry < table.size() && std::memcmp(&table[entry], &fp, sizeof fp)) entry++;
            if (entry == table.size()) table.push_back(fp);
            if (!densrec::fits_window(fp, bin_w, bin_h)) {
                slow.push_back(densrec::SlowSlot{(uint32_t)s, entry});
                num_slow_cells += s < d.first_filler_slot;
                entry = densrec::NO_DEPOSIT;
            } else {
                out.fast++;
            }
        }
        idx[s / pinrec::LANES].idx[s % pinrec::LANES] = entry;
    }
    out.slow = (long)slow.size();
    out.slow_cells = num_slow_cells;
    out.rho.resize(num_bins / pinrec::LANES);
    bin_density_CU(pos_x.data(), pos_y.data(), idx.data(), (int)(num_slots / pinrec::LANES),
                   (int)(d.first_filler_slot / pinrec::LANES), slow.data(), num_slow_cells, (int)slow.size(),
                   table.data(), (int)table.size(), fixed_base.data(), bins, bin_w, bin_h, fixed_scale,
                   std::ldexp(1.0f, -out.frac_bits), densrec::to_fixed(bin_w * bin_h * d.target_density * fixed_scale),
                   out.rho.data(), out.excess);
    return out;
}

static bool run(const SyntheticDesign& d, int bins, const Bounds& bound) {
    const double REL_RMS_MAX = bound.rel_rms, PEAK_REL_MAX = bound.peak_rel, MASS_REL_MAX = bound.mass_rel,
                 OVERFLOW_REL_MAX = bound.overflow_rel;
    const float bin_w = d.die / bins, bin_h = d.die / bins, bin_area = bin_w * bin_h;
    const size_t num_bins = (size_t)bins * bins;
    const long num_slots = d.num_slots();

    // Golden: fixed base map (deposited, then capped), the cells, the overflow there, the fillers. Meow.
    std::vector<double> golden(num_bins, 0.0);
    for (size_t i = 0; i < d.fixed_x.size(); i++)
        golden_deposit(d.fixed_x[i], d.fixed_y[i], d.fixed_w[i], d.fixed_h[i], false, d.target_density, bins, bin_w, bin_h, golden);
    for (double& v : golden) v = std::min(v, (double)bin_area * d.target_density);
    const std::vector<double> base_area = golden;

    const double cap = (double)bin_w * bin_h * d.target_density;
    double movable_area = 0;
    for (long s = 0; s < d.first_filler_slot; s++) movable_area += (double)d.w[s] * d.h[s];
    auto overflow_of = [&](const std::vector<double>& overlap) {
        double area = 0;
        for (double v : overlap) area += std::max(0.0, v - cap);
        return area / movable_area;
    };
    double golden_overflow = 0;
    for (long s = 0; s < num_slots; s++) {
        if (s == d.first_filler_slot) golden_overflow = overflow_of(golden);
        if (d.deposits[s]) golden_deposit(d.x[s], d.y[s], d.w[s], d.h[s], d.is_macro[s], d.target_density, bins, bin_w, bin_h, golden);
    }

    const DeviceRun device = run_device(d, bins, base_area);
    // sw_flat Placer::overflow_from_excess, the host's half of the overflow. Meow.
    const float overflow = (float)std::ldexp((double)device.excess, -device.frac_bits) * bin_area / ((float)movable_area + 1e-8f);
    const double overflow_rel = std::fabs(overflow - golden_overflow) / golden_overflow;
    const double filler_rel = std::fabs(overflow_of(golden) - golden_overflow) / golden_overflow;
    const bool overflow_ok = overflow_rel < OVERFLOW_REL_MAX && filler_rel > OVERFLOW_REL_MAX;

    double err_sq = 0, ref_sq = 0, max_err = 0, peak = 0, device_peak = 0, mass = 0, golden_mass = 0;
    bool finite = true;
    for (size_t bin = 0; bin < num_bins; bin++) {
        const double value = device.rho[bin / pinrec::LANES].v[bin % pinrec::LANES];
        const double expect = golden[bin] / bin_area;
        finite &= std::isfinite(value);
        err_sq += (value - expect) * (value - expect);
        ref_sq += expect * expect;
        max_err = std::max(max_err, std::fabs(value - expect));
        peak = std::max(peak, expect);
        device_peak = std::max(device_peak, value);
        mass += value * bin_area;
        golden_mass += golden[bin];
    }
    const double rel_rms = std::sqrt(err_sq / ref_sq), peak_rel = max_err / peak, mass_rel = std::fabs(mass - golden_mass) / golden_mass;
    const bool word_ok = device_peak < std::ldexp(1.0, densrec::URAM_WORD_WIDTH - device.frac_bits);
    const bool paths_ok = device.fast > 0 && device.slow_cells > 0;

    const SyntheticDesign other_order = shuffled(d, 11);
    const DeviceRun reordered = run_device(other_order, bins, base_area);
    const std::vector<float> float_a = float_deposit(d, bins), float_b = float_deposit(other_order, bins);
    long float_differ = 0;
    for (size_t bin = 0; bin < num_bins; bin++) float_differ += std::memcmp(&float_a[bin], &float_b[bin], sizeof(float)) != 0;
    const bool order_ok = reordered.excess == device.excess && float_differ > 0 &&
                          std::memcmp(reordered.rho.data(), device.rho.data(), device.rho.size() * sizeof(pinrec::SlotBeat)) == 0;

    const float big_macro = d.w[20000 + 5];   // the largest macro
    const bool coverage = big_macro / bin_w >= 8.0f;
    bool clipped = false;
    for (long s = 0; s < num_slots && !clipped; s++) clipped = d.deposits[s] && (d.x[s] < 0 || d.y[s] < 0);

    const bool pass = finite && rel_rms < REL_RMS_MAX && peak_rel < PEAK_REL_MAX && mass_rel < MASS_REL_MAX && coverage &&
                      clipped && overflow_ok && order_ok && word_ok && paths_ok;
    printf("  bins=%4d  rel_rms=%.3g (<%g)  peak_rel=%.3g (<%g)  mass_rel=%.3g (<%g)  finite=%d  macro_bins=%.1f  clipped=%d\n"
           "            overflow=%.6g golden=%.6g rel=%.3g (<%g), with fillers rel=%.3g (>%g)\n"
           "            order: shuffled slots %s (a float accumulator differs in %ld bins)  word: peak %.4g bin areas (< 2^%d)\n"
           "            paths: %ld fast, %ld slow (%ld cells), F = %d  %s\n",
           bins, rel_rms, REL_RMS_MAX, peak_rel, PEAK_REL_MAX, mass_rel, MASS_REL_MAX, finite, big_macro / bin_w, clipped,
           overflow, golden_overflow, overflow_rel, OVERFLOW_REL_MAX, filler_rel, OVERFLOW_REL_MAX,
           order_ok ? "bit-identical" : "DIFFER", float_differ, device_peak, densrec::URAM_WORD_WIDTH - device.frac_bits,
           device.fast, device.slow, device.slow_cells, device.frac_bits, pass ? "PASS" : "FAIL");
    return pass;
}

int main() {
    bool pass = true;
    pass &= run(make_design(7, 2000.0f), 128, BOUNDS_128);
    pass &= run(make_design(7, 16000.0f), 1024, BOUNDS_1024);
    printf(pass ? "bin_density_CU_test: PASS\n" : "bin_density_CU_test: FAIL\n");
    return pass ? 0 : 1;
}
