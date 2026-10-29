// cosim_tb.cpp -- RTL co-simulation bench for bin_density_CU: 8 producers, 64 banks, forwarding (#48).
//
// drain_banks reads a bank word, adds, and writes it back several cycles later, so an update that hits
// the same word inside that window must get the sum through the forwarding registers. A beat deals lanes
// 2p, 2p+1 to producer p, so the bench builds one cell list per producer and lays producer p's j-th cell
// at beat j / 2, lane 2p + j % 2. The lists are ../../bin_density_banked/cosim/cosim_tb.cpp's, built to
// re-hit bank words ACROSS producers:
//   distance 1   producers 0..5, 64 cells each at ONE position: the same 16 bank words every update
//   distance k   for k = 2..8, 48 cells each cycling k positions 8 bins apart (same banks, different
//                addresses), phase-shifted per producer
// Producer 0 fills every slot: it has the banks' priority, so while its FIFOs hold updates its banks see
// an unbroken distance-k stream. Producer p >= 1 leaves every (p+1)th slot NO_DEPOSIT, so once producer 0
// moves on the pick interleaves producers 1..5 at those distances. Then a random tail of unequal length
// per producer (300 .. 0 cells, NO_DEPOSIT mixed in, some clipped at the die edge), so the `last`s reach
// the banks at very different times; one macro on the slow path. The filler region repeats the
// distance-1 pile-up on every producer, then random fillers. A fixed base under all of it.
// Run at grid 64 (= GRID_MAX) and 48 (< GRID_MAX: the fixed bank pitch leaves words unused).
//
// Golden: the plain deposit, one bin at a time in slot order, uint64, no banks; the excess at the barrier.
// rho and the excess must match BIT FOR BIT. C simulation runs the processes one after another, so it
// passes at any depth >= 1 (depth 0 fails even in C: the deferred write-back stores a sum one iteration
// after the next read); the RTL passes only if BIN_FORWARD_DEPTH covers the scheduled read-to-write
// distance. Exit 0 = match. Meow.

#include "modules/bin_density_CU.hpp"

#include <algorithm>
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

constexpr int BEATS_MAX = 400, TABLE_DEPTH = 16, GRID_BEATS_MAX = densrec::GRID_MAX * densrec::GRID_MAX / pinrec::LANES;
constexpr float BIN_W = 4.0f, BIN_H = 4.0f, TD = 0.8f;
constexpr int TAIL_CELLS[PRODUCERS] = {300, 40, 150, 0, 220, 90, 0, 3};
constexpr uint32_t FILLER = 3, MACRO = 4;

struct Cell { float x, y; uint32_t footprint; };

// Lay per-producer lists out as beats from `first_beat`: producer p's j-th cell at lane 2p + j % 2. Meow.
static int lay_out(const std::vector<std::vector<Cell>>& lists, int first_beat, std::vector<Cell>& slots) {
    size_t longest = 0;
    for (const auto& list : lists) longest = std::max(longest, list.size());
    const int beats = (int)(longest + SLOTS_PER_DEAL - 1) / SLOTS_PER_DEAL;
    slots.resize((size_t)(first_beat + beats) * pinrec::LANES, Cell{NAN, NAN, densrec::NO_DEPOSIT});
    for (int p = 0; p < PRODUCERS; p++)
        for (size_t j = 0; j < lists[p].size(); j++)
            slots[(size_t)(first_beat + j / SLOTS_PER_DEAL) * pinrec::LANES + SLOTS_PER_DEAL * p + j % SLOTS_PER_DEAL] = lists[p][j];
    return beats;
}

static int check_grid(int grid, std::mt19937& rng) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float die = grid * BIN_W, bin_area = BIN_W * BIN_H;

    // Footprint table: three cell sizes, the filler, one macro (slow path); padded to the m_axi depth. Meow.
    std::vector<densrec::Footprint> table(TABLE_DEPTH);
    const float widths[] = {5.0f, 7.0f, 3.0f, 4.5f};
    for (int t = 0; t < 4; t++) {
        table[t] = densrec::make_footprint(widths[t], 4.0f, false, BIN_W, BIN_H, TD, true);
        if (!densrec::fits_window(table[t], BIN_W, BIN_H)) { printf("FAIL: footprint %d too wide\n", t); return 1; }
    }
    table[MACRO] = densrec::make_footprint(40.0f, 36.0f, true, BIN_W, BIN_H, TD, true);
    const int num_footprints = 5;

    std::vector<std::vector<Cell>> cells(PRODUCERS), fillers(PRODUCERS);
    auto gap = [](int p, int j) { return p > 0 && j % (p + 1) == 1; };
    for (int p = 0; p < 6; p++) {
        int j = 0;
        for (int i = 0; i < 64; i++, j++) cells[p].push_back(Cell{100.3f, 60.7f, gap(p, j) ? densrec::NO_DEPOSIT : (uint32_t)(i % 3)});
        for (int k = 2; k <= 8; k++)
            for (int i = 0; i < 48; i++, j++)   // 32 units = 8 bins: same bank, next address
                cells[p].push_back(Cell{2.3f + 32.0f * ((i + p) % k), 150.5f + 3.0f * k, gap(p, j) ? densrec::NO_DEPOSIT : (uint32_t)((i + p) % 3)});
    }
    for (int p = 0; p < PRODUCERS; p++)
        for (int i = 0; i < TAIL_CELLS[p]; i++)
            cells[p].push_back(Cell{unit(rng) * (die + 4.0f) - 6.0f, unit(rng) * (die + 4.0f) - 6.0f,
                                    i % 9 == 4 ? densrec::NO_DEPOSIT : (uint32_t)(i % 3)});
    cells[3].push_back(Cell{70.0f, 30.0f, MACRO});
    for (int p = 0; p < PRODUCERS; p++) {
        for (int i = 0; i < 16; i++) fillers[p].push_back(Cell{100.3f, 60.7f, FILLER});
        for (int i = 0; i < 2 * p; i++) fillers[p].push_back(Cell{unit(rng) * (die - 6.0f), unit(rng) * (die - 6.0f), FILLER});
    }
    std::vector<Cell> slots;
    const int cell_beats = lay_out(cells, 0, slots);
    const int num_beats = cell_beats + lay_out(fillers, cell_beats, slots);
    if (num_beats > BEATS_MAX) { printf("FAIL: %d beats exceed the m_axi depth\n", num_beats); return 1; }
    const long num_slots = (long)num_beats * pinrec::LANES, first_filler = (long)cell_beats * pinrec::LANES;

    // Fixed base: a few capped bins. Total-area F, as the host computes it. Meow.
    std::vector<float> base((size_t)grid * grid, 0.0f);
    for (size_t b = 0; b < base.size(); b++) if (b % 37 == 0) base[b] = bin_area * TD;
    double total = 0;
    for (float a : base) total += a;
    for (const Cell& c : slots)
        if (c.footprint != densrec::NO_DEPOSIT) total += (double)table[c.footprint].weight * 4.0 * table[c.footprint].half_cw * table[c.footprint].half_ch;
    const int frac_bits = densrec::rho_frac_bits(total / bin_area);
    const float fixed_scale = std::ldexp(1.0f, frac_bits) / bin_area, rho_unit = std::ldexp(1.0f, -frac_bits);
    const uint32_t cap = densrec::to_fixed(BIN_W * BIN_H * TD * fixed_scale);

    // Golden: one bin at a time, slot order, plain integers. Meow.
    std::vector<uint64_t> golden((size_t)grid * grid);
    for (size_t b = 0; b < golden.size(); b++) golden[b] = densrec::to_fixed(base[b] * fixed_scale);
    uint64_t golden_excess = 0;
    for (long i = 0; i < num_slots; i++) {
        if (i == first_filler)
            for (uint64_t q : golden) golden_excess += q > cap ? q - cap : 0;
        if (slots[i].footprint == densrec::NO_DEPOSIT) continue;
        densrec::Footprint fp = table[slots[i].footprint];
        fp.weight *= fixed_scale;
        const Window w = footprint_window(slots[i].x, slots[i].y, fp, grid, 1.0f / BIN_W, 1.0f / BIN_H);
        for (int col = w.col_lo; col <= w.col_hi; col++) {
            const float ow = overlap_x(w, col, BIN_W);
            if (ow <= 0.0f) continue;
            for (int row = w.row_lo; row <= w.row_hi; row++) {
                const float oh = overlap_y(w, row, BIN_H);
                if (oh > 0.0f) golden[(size_t)col * grid + row] += densrec::to_fixed(ow * oh * fp.weight);
            }
        }
    }

    // Device inputs, padded to the m_axi depths; the macro leaves the stream for the slow list. Meow.
    std::vector<pinrec::SlotBeat> pos_x(BEATS_MAX), pos_y(BEATS_MAX);
    std::vector<densrec::FootprintBeat> index(BEATS_MAX);
    std::vector<densrec::SlowSlot> slow(8);
    int num_slow = 0;
    for (long i = 0; i < num_slots; i++) {
        pos_x[i / pinrec::LANES].v[i % pinrec::LANES] = slots[i].x;
        pos_y[i / pinrec::LANES].v[i % pinrec::LANES] = slots[i].y;
        uint32_t entry = slots[i].footprint;
        if (entry == MACRO) { slow[num_slow++] = densrec::SlowSlot{(uint32_t)i, MACRO}; entry = densrec::NO_DEPOSIT; }
        index[i / pinrec::LANES].idx[i % pinrec::LANES] = entry;
    }
    std::vector<densrec::FixedBeat> fixed_base(GRID_BEATS_MAX);
    for (size_t b = 0; b < base.size(); b++) fixed_base[b / pinrec::LANES].q[b % pinrec::LANES] = densrec::to_fixed(base[b] * fixed_scale);
    std::vector<pinrec::SlotBeat> rho(GRID_BEATS_MAX);
    unsigned long long excess = 0;

    bin_density_CU_top(pos_x.data(), pos_y.data(), index.data(), num_beats, cell_beats, slow.data(), num_slow, num_slow,
                       table.data(), num_footprints, fixed_base.data(), grid, BIN_W, BIN_H, 1.0f / BIN_W, 1.0f / BIN_H,
                       fixed_scale, rho_unit, cap, rho.data(), &excess);

    int differ = 0, first = -1;
    for (int b = 0; b < grid * grid; b++) {
        const float expect = (float)golden[b] * rho_unit;
        const float got = rho[b / pinrec::LANES].v[b % pinrec::LANES];
        if (std::memcmp(&expect, &got, sizeof got)) { differ++; if (first < 0) first = b; }
    }
    const bool ok = differ == 0 && excess == golden_excess && num_slow == 1;
    printf("bin_density_CU cosim: forward depth %d, grid %d, F = %d, %d cell + %d filler beats: %d of %d bins differ",
           FORWARD_DEPTH, grid, frac_bits, cell_beats, num_beats - cell_beats, differ, grid * grid);
    if (first >= 0)
        printf(" (first: bin %d, col %d row %d: got %.9g, expect %.9g)", first, first / grid, first % grid,
               rho[first / pinrec::LANES].v[first % pinrec::LANES], (float)golden[first] * rho_unit);
    printf("; excess %llu vs %llu -> %s\n", excess, (unsigned long long)golden_excess, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int main() {
    std::mt19937 rng(48);
    int failed = 0;
    for (int grid : {densrec::GRID_MAX, densrec::GRID_MAX - 16}) failed += check_grid(grid, rng);
    printf("bin_density_CU cosim: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
