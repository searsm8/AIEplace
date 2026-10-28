// cosim_tb.cpp -- RTL co-simulation bench for bin_density_CU's banked deposit and its forwarding (#48).
//
// The stream is built to collide: the deposit pipeline reads a bin, adds, and writes it back several
// cycles later, so a cell that hits the same bin within that window must get the sum through the
// forwarding registers, not from the URAM. The cells come in sections:
//   distance 1   128 cells at ONE position: every cycle re-hits the same bins
//   distance k   for k = 2..5, 64 cells cycling through k far-apart positions: the same bins every k cycles
//   random       the rest, plus NO_DEPOSIT slots, and one macro on the slow path
// then a random filler tail. Golden: the same deposit done the plain way here, one bin at a time in
// slot order, uint64 adds, no banks, no forwarding. rho and the overflow excess must match BIT FOR BIT.
//
// C simulation runs the pipeline in order, so it always passes; the RTL only passes if
// BIN_FORWARD_DEPTH covers the scheduled read-to-write distance. Exit 0 = match. Meow.

#include "modules/bin_density_CU.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace plalgo;

extern "C" void bin_density_CU_top(const pinrec::SlotBeat*, const pinrec::SlotBeat*, const densrec::FootprintBeat*, int, int,
                                   const densrec::SlowSlot*, int, int, const densrec::Footprint*, int,
                                   const densrec::FixedBeat*, int, float, float, float, float, float, float, unsigned int,
                                   pinrec::SlotBeat*, unsigned long long*);

constexpr int BINS = 64, NUM_BEATS = 64, NUM_SLOTS = NUM_BEATS * pinrec::LANES, CELL_BEATS = 48;
constexpr float BIN_W = 4.0f, BIN_H = 4.0f, TD = 0.8f;

int main() {
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float die = BINS * BIN_W;

    // Footprint table: three cell sizes, the filler, one macro (slow path). Meow.
    std::vector<densrec::Footprint> table = {
        densrec::make_footprint(5.0f, 4.0f, false, BIN_W, BIN_H, TD, true),
        densrec::make_footprint(7.0f, 4.0f, false, BIN_W, BIN_H, TD, true),
        densrec::make_footprint(3.0f, 4.0f, false, BIN_W, BIN_H, TD, true),
        densrec::make_footprint(4.5f, 4.0f, false, BIN_W, BIN_H, TD, true),    // the filler
        densrec::make_footprint(40.0f, 36.0f, true, BIN_W, BIN_H, TD, true),   // the macro
    };
    for (int i = 0; i < 4; i++) if (!densrec::fits_window(table[i], BIN_W, BIN_H)) { printf("FAIL: footprint %d too wide\n", i); return 1; }

    std::vector<float> x(NUM_SLOTS, NAN), y(NUM_SLOTS, NAN);
    std::vector<uint32_t> idx(NUM_SLOTS, densrec::NO_DEPOSIT);
    auto place = [&](int s, float px, float py, uint32_t type) { x[s] = px; y[s] = py; idx[s] = type; };
    int s = 0;
    for (int i = 0; i < 128; i++, s++) place(s, 100.3f, 60.7f, i % 3);                    // distance 1
    for (int k = 2; k <= 5; k++)
        for (int i = 0; i < 64; i++, s++) place(s, 20.0f + 45.0f * (i % k), 150.5f + 3.0f * k, i % 3);   // distance k
    for (; s < CELL_BEATS * pinrec::LANES; s++)
        if (s % 9 == 4) continue;                                                         // NO_DEPOSIT
        else place(s, unit(rng) * (die - 8.0f), unit(rng) * (die - 8.0f), s % 3);
    const int macro_slot = CELL_BEATS * pinrec::LANES - 1;
    place(macro_slot, 170.0f, 30.0f, 4);
    for (; s < NUM_SLOTS; s++) place(s, unit(rng) * (die - 6.0f), unit(rng) * (die - 6.0f), 3);   // fillers

    // Fixed base: a few capped blocks. Total-area F, as the host computes it. Meow.
    const float bin_area = BIN_W * BIN_H;
    std::vector<float> base(BINS * BINS, 0.0f);
    for (int b = 0; b < BINS * BINS; b++) if (b % 37 == 0) base[b] = bin_area * TD;
    double total = 0;
    for (float a : base) total += a;
    for (int i = 0; i < NUM_SLOTS; i++)
        if (idx[i] != densrec::NO_DEPOSIT) total += (double)table[idx[i]].weight * 4.0 * table[idx[i]].half_cw * table[idx[i]].half_ch;
    const int frac_bits = densrec::rho_frac_bits(total / bin_area);
    const float fixed_scale = std::ldexp(1.0f, frac_bits) / bin_area, rho_unit = std::ldexp(1.0f, -frac_bits);
    const uint32_t cap = densrec::to_fixed(BIN_W * BIN_H * TD * fixed_scale);

    // Golden: one bin at a time, slot order, plain integers. Meow.
    std::vector<uint64_t> golden(BINS * BINS);
    for (int b = 0; b < BINS * BINS; b++) golden[b] = densrec::to_fixed(base[b] * fixed_scale);
    uint64_t golden_excess = 0;
    for (int i = 0; i < NUM_SLOTS; i++) {
        if (i == CELL_BEATS * pinrec::LANES)
            for (uint64_t q : golden) golden_excess += q > cap ? q - cap : 0;
        if (idx[i] == densrec::NO_DEPOSIT) continue;
        densrec::Footprint fp = table[idx[i]];
        fp.weight *= fixed_scale;
        const Window w = footprint_window(x[i], y[i], fp, BINS, 1.0f / BIN_W, 1.0f / BIN_H);
        for (int col = w.col_lo; col <= w.col_hi; col++) {
            const float ow = overlap_x(w, col, BIN_W);
            if (ow <= 0.0f) continue;
            for (int row = w.row_lo; row <= w.row_hi; row++) {
                const float oh = overlap_y(w, row, BIN_H);
                if (oh > 0.0f) golden[col * BINS + row] += densrec::to_fixed(ow * oh * fp.weight);
            }
        }
    }

    // Device inputs: the macro leaves the stream for the slow list. Meow.
    std::vector<pinrec::SlotBeat> pos_x(NUM_BEATS), pos_y(NUM_BEATS);
    std::vector<densrec::FootprintBeat> index(NUM_BEATS);
    std::vector<densrec::SlowSlot> slow = {densrec::SlowSlot{(uint32_t)macro_slot, 4}};
    for (int i = 0; i < NUM_SLOTS; i++) {
        pos_x[i / pinrec::LANES].v[i % pinrec::LANES] = x[i];
        pos_y[i / pinrec::LANES].v[i % pinrec::LANES] = y[i];
        index[i / pinrec::LANES].idx[i % pinrec::LANES] = i == macro_slot ? densrec::NO_DEPOSIT : idx[i];
    }
    std::vector<densrec::FixedBeat> fixed_base(BINS * BINS / pinrec::LANES);
    for (int b = 0; b < BINS * BINS; b++) fixed_base[b / pinrec::LANES].q[b % pinrec::LANES] = densrec::to_fixed(base[b] * fixed_scale);
    std::vector<pinrec::SlotBeat> rho(BINS * BINS / pinrec::LANES);
    unsigned long long excess = 0;

    bin_density_CU_top(pos_x.data(), pos_y.data(), index.data(), NUM_BEATS, CELL_BEATS, slow.data(), 1, 1, table.data(),
                       (int)table.size(), fixed_base.data(), BINS, BIN_W, BIN_H, 1.0f / BIN_W, 1.0f / BIN_H, fixed_scale, rho_unit, cap, rho.data(), &excess);

    int differ = 0, first = -1;
    for (int b = 0; b < BINS * BINS; b++) {
        const float expect = (float)golden[b] * rho_unit;
        const float got = rho[b / pinrec::LANES].v[b % pinrec::LANES];
        if (std::memcmp(&expect, &got, sizeof got)) { differ++; if (first < 0) first = b; }
    }
    const bool ok = differ == 0 && excess == golden_excess;
    printf("bin_density_CU cosim: forward depth %d, F = %d: %d of %d bins differ", FORWARD_DEPTH, frac_bits, differ, BINS * BINS);
    if (first >= 0)
        printf(" (first: bin %d, col %d row %d: got %.9g, expect %.9g)", first, first / BINS, first % BINS,
               rho[first / pinrec::LANES].v[first % pinrec::LANES], (float)golden[first] * rho_unit);
    printf("; excess %llu vs %llu -> %s\n", excess, (unsigned long long)golden_excess, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
