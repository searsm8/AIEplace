// cosim_tb.cpp -- RTL co-simulation bench for the 64-bank deposit (cosim_top.cpp) and its forwarding (#48).
//
// drain_banks reads a bank word, adds, and writes it back several cycles later, so an update that hits
// the same bank address inside that window must get the sum through the forwarding registers. The 8
// producers' slices are built so the banks re-hit addresses ACROSS producers:
//   distance 1   producers 0..5, 64 cells each at ONE position: the same 16 bank words every update
//   distance k   for k = 2..8, 48 cells each cycling k positions 8 bins apart (same banks, different
//                addresses), phase-shifted per producer
// Producer 0 fills every slot: it has the banks' priority, so while its FIFOs hold updates its banks see
// an unbroken distance-k stream. Producer p >= 1 leaves every (p+1)th slot NO_DEPOSIT, so once producer 0
// moves on, no single FIFO keeps a bank busy and the pick interleaves producers 1..5 at those distances.
// Then a random tail of unequal length per producer (300 .. 0 cells, NO_DEPOSIT mixed in, some clipped at
// the die edge); producer 6 has no cells at all and producer 7 three, so the `last`s reach the banks at
// very different times. Run at grid 64 (= GRID_MAX) and 60 (< GRID_MAX, not a multiple of 8: the fixed
// bank pitch leaves words past the grid, which must stay 0).
//
// Golden: the plain deposit, one bin at a time, uint64, no banks. Every word must match BIT FOR BIT.
// C simulation runs the processes one after another, so it passes at any depth >= 1 (depth 0 fails even in
// C: the deferred write-back stores a sum one iteration after the next read, so slot 0 must be compared);
// the RTL passes only if BANKED_FORWARD_DEPTH covers the scheduled read-to-write distance. Exit 0 = match. Meow.

#include "modules/bin_density_CU.hpp"
#include "../../bin_density_producer/producer.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#ifndef BANKED_FORWARD_DEPTH
#define BANKED_FORWARD_DEPTH 6   // cosim.tcl passes it; this default matches ../banked_top.cpp. Meow.
#endif

#define ARGS(p) const densrec::Footprint*, const SlotIn*, int
extern "C" void banked_cosim_top(ARGS(0), ARGS(1), ARGS(2), ARGS(3), ARGS(4), ARGS(5), ARGS(6), ARGS(7), int, int,
                                 float, float, float, unsigned long long*);

constexpr int PRODUCERS = 8, SLOTS_MAX = 1024, TABLE_MAX = 16, BANK_SIDE = 8;
constexpr int RAW_WORDS = densrec::GRID_MAX * densrec::GRID_MAX;
constexpr float BIN_W = 4.0f, BIN_H = 4.0f, TD = 0.8f;
constexpr int TAIL_CELLS[PRODUCERS] = {300, 40, 150, 0, 220, 90, 0, 3};

static int check_grid(int grid, std::mt19937& rng) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float die = grid * BIN_W, bin_area = BIN_W * BIN_H;

    std::vector<densrec::Footprint> table(TABLE_MAX);   // padded to the m_axi depth
    const float widths[] = {5.0f, 7.0f, 3.0f, 4.5f};
    for (int t = 0; t < 4; t++) {
        table[t] = densrec::make_footprint(widths[t], 4.0f, false, BIN_W, BIN_H, TD, true);
        if (!densrec::fits_window(table[t], BIN_W, BIN_H)) { printf("FAIL: footprint %d too wide\n", t); return 1; }
    }
    const int num_footprints = 4;

    std::vector<std::vector<SlotIn>> slices(PRODUCERS);
    auto add = [&](int p, float x, float y, uint32_t type) { slices[p].push_back(SlotIn{x, y, type, 0}); };
    auto gap = [](int p, int j) { return p > 0 && j % (p + 1) == 1; };
    for (int p = 0; p < 6; p++) {
        int j = 0;
        for (int i = 0; i < 64; i++, j++) add(p, 100.3f, 60.7f, gap(p, j) ? densrec::NO_DEPOSIT : i % 3);
        for (int k = 2; k <= 8; k++)
            for (int i = 0; i < 48; i++, j++)   // 32 units = 8 bins: same bank, next address
                add(p, 2.3f + 32.0f * ((i + p) % k), 150.5f + 3.0f * k, gap(p, j) ? densrec::NO_DEPOSIT : (i + p) % 3);
    }
    for (int p = 0; p < PRODUCERS; p++)
        for (int i = 0; i < TAIL_CELLS[p]; i++)
            add(p, unit(rng) * (die + 4.0f) - 6.0f, unit(rng) * (die + 4.0f) - 6.0f, i % 9 == 4 ? densrec::NO_DEPOSIT : i % 4);

    double total_area = 0;
    for (const auto& slice : slices)
        for (const SlotIn& s : slice)
            if (s.footprint != densrec::NO_DEPOSIT) {
                const densrec::Footprint& fp = table[s.footprint];
                total_area += (double)fp.weight * 4.0 * fp.half_cw * fp.half_ch;
            }
    const int frac_bits = densrec::rho_frac_bits(total_area / bin_area);
    const float fixed_scale = std::ldexp(1.0f, frac_bits) / bin_area;

    std::vector<uint64_t> golden((size_t)grid * grid, 0);
    for (const auto& slice : slices)
        for (const SlotIn& s : slice) {
            if (s.footprint == densrec::NO_DEPOSIT) continue;
            densrec::Footprint fp = table[s.footprint];
            fp.weight *= fixed_scale;
            const plalgo::Window w = plalgo::footprint_window(s.x, s.y, fp, grid, 1.0f / BIN_W, 1.0f / BIN_H);
            for (int col = w.col_lo; col <= w.col_hi; col++) {
                const float ow = plalgo::overlap_x(w, col, BIN_W);
                if (ow <= 0.0f) continue;
                for (int row = w.row_lo; row <= w.row_hi; row++) {
                    const float oh = plalgo::overlap_y(w, row, BIN_H);
                    if (oh > 0.0f) golden[(size_t)col * grid + row] += densrec::to_fixed(ow * oh * fp.weight);
                }
            }
        }

    std::vector<std::vector<SlotIn>> buffers(PRODUCERS, std::vector<SlotIn>(SLOTS_MAX));   // m_axi depth
    std::vector<std::vector<densrec::Footprint>> tables(PRODUCERS, table);
    for (int p = 0; p < PRODUCERS; p++) {
        if ((int)slices[p].size() > SLOTS_MAX) { printf("FAIL: slice %d exceeds the m_axi depth\n", p); return 1; }
        std::copy(slices[p].begin(), slices[p].end(), buffers[p].begin());
    }
    std::vector<unsigned long long> raw(RAW_WORDS, ~0ull);
#define SLICE(p) tables[p].data(), buffers[p].data(), (int)slices[p].size()
    banked_cosim_top(SLICE(0), SLICE(1), SLICE(2), SLICE(3), SLICE(4), SLICE(5), SLICE(6), SLICE(7), num_footprints, grid,
                     BIN_W, BIN_H, fixed_scale, raw.data());

    int differ = 0, first = -1;
    const int pitch = densrec::GRID_MAX / BANK_SIDE;
    for (int i = 0; i < RAW_WORDS; i++) {
        const int bank = i % 64, addr = i / 64;
        const int col = (addr / pitch) * BANK_SIDE + bank / BANK_SIDE, row = (addr % pitch) * BANK_SIDE + bank % BANK_SIDE;
        const uint64_t want = col < grid && row < grid ? golden[(size_t)col * grid + row] : 0;
        if (raw[i] != want) { differ++; if (first < 0) first = i; }
    }
    printf("banked cosim: forward depth %d, grid %d, F = %d, slices", BANKED_FORWARD_DEPTH, grid, frac_bits);
    for (const auto& slice : slices) printf(" %zu", slice.size());
    printf(": %d of %d words differ", differ, RAW_WORDS);
    if (first >= 0) printf(" (first: bank %d addr %d: got %llu)", first % 64, first / 64, raw[first]);
    printf(" -> %s\n", differ ? "FAIL" : "PASS");
    return differ ? 1 : 0;
}

int main() {
    std::mt19937 rng(48);
    int failed = 0;
    for (int grid : {densrec::GRID_MAX, densrec::GRID_MAX - 4}) failed += check_grid(grid, rng);
    printf("banked cosim: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
