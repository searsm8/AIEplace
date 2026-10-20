// bank_fifo_model -- #48, Mark's idea (2026-10-20): put a FIFO in front of every bank. A producer computes
// a cell's footprint and pushes one update per covered bin into that bin's bank FIFO; every bank pops one
// update per cycle and does its read-add-write. A cell no longer needs all its banks in the same cycle.
//
//   bank_fifo_model <config.toml> [max_iterations, 0 = to the stop] [stride: simulate every Nth iteration]
//
// Banks: 16 (bank = 4*(col%4) + row%4) or 64 (bank = 8*(col%8) + row%8). At 1024, 64 banks of 16K words
// are 2 URAMs each: still 128 URAMs. Any <= 4x4 window touches distinct banks either way.
// Per cycle: every non-empty bank FIFO pops one update; then up to P producers take the next cells in
// stream order, each pushing its updates, but a FIFO takes at most W pushes per cycle and holds at most
// DEPTH; the first cell that does not fit stalls the producers (in order) until the next cycle. The
// overflow barrier (cells, then fillers) drains every FIFO once. Fast slots only, as lane_model.
// Speedup = (slots, one per cycle, as the CU is today) / cycles. Also printed: updates per cell, and the
// ceiling banks / updates-per-cell (every bank busy every cycle). Meow.

#include "density_inputs.hpp"
#include "modules/bin_density_CU.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace sw_flat;

namespace {

constexpr int DEPTH = 32;

struct Config { int banks, producers, writes, readers; };
const Config CONFIGS[] = {
    {16, 2, 1, 1}, {16, 4, 1, 1}, {16, 4, 2, 1},
    {16, 4, 4, 1},
    {64, 4, 1, 1}, {64, 8, 1, 1}, {64, 8, 2, 1}, {64, 16, 2, 1}, {64, 8, 2, 4}, {64, 16, 2, 4},
    {64, 8, 4, 4}, {64, 16, 4, 4},
};
constexpr int NUM_CONFIGS = sizeof(CONFIGS) / sizeof(CONFIGS[0]);

struct Cell { uint16_t mask16; uint64_t mask64; };

std::vector<size_t> read_order(size_t first, size_t last, int readers) {   // slot indices, readers in turn
    std::vector<size_t> order;
    const size_t n = last - first, per = (n + readers - 1) / readers;
    for (size_t k = 0; k < per; k++)
        for (int r = 0; r < readers; r++)
            if (r * per + k < n) order.push_back(first + r * per + k);
    return order;
}

template <typename Mask>
double simulate(const std::vector<Mask>& cells, int banks, int producers, int writes) {
    std::vector<int> occupancy(banks, 0), pushed(banks, 0);
    size_t next = 0;
    long cycles = 0;
    while (true) {
        for (int b = 0; b < banks; b++) if (occupancy[b] > 0) occupancy[b]--;
        std::fill(pushed.begin(), pushed.end(), 0);
        for (int p = 0; p < producers && next < cells.size(); p++) {
            const Mask m = cells[next];
            bool fits = true;
            for (Mask rest = m; rest && fits; rest &= rest - 1) {
                const int b = __builtin_ctzll((unsigned long long)rest);
                fits = occupancy[b] < DEPTH && pushed[b] < writes;
            }
            if (!fits) break;
            for (Mask rest = m; rest; rest &= rest - 1) {
                const int b = __builtin_ctzll((unsigned long long)rest);
                occupancy[b]++; pushed[b]++;
            }
            next++;
        }
        cycles++;
        if (next == cells.size() && std::all_of(occupancy.begin(), occupancy.end(), [](int o) { return o == 0; }))
            return (double)cycles;
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: bank_fifo_model <config.toml> [max_iterations] [stride]\n"); return 2; }
    const int max_iterations = argc > 2 ? atoi(argv[2]) : 0;
    const int stride = argc > 3 ? atoi(argv[3]) : 1;
    PlacerConfig cfg = read_placer_config(argv[1]);
    cfg.dump_schedule_trace = false;
    cfg.deposit_fixed = true;
    Placer placer;
    placer.setup(cfg);
    placer.initialize_placement();
    placer.compute_hpwl_partials();
    placer.compute_electric_fields();
    placer.initialize_density_weight();

    const DensityInputs in = make_density_inputs(placer);
    const int bins = placer.bins;
    const long num_slots = placer.layout.num_slots;
    const size_t first_filler = (size_t)placer.layout.first_filler_slot;
    printf("%s grid %d: %ld slots; FIFO depth %d; simulated every %d iterations\n", placer.design.name.c_str(), bins,
           num_slots, DEPTH, stride);

    std::vector<std::vector<size_t>> orders[2];   // [cells, fillers] per reader count 1 and 4
    for (int r : {1, 4}) {
        std::vector<std::vector<size_t>> o = {read_order(0, first_filler, r), read_order(first_filler, num_slots, r)};
        orders[r == 4] = o;
    }
    double total_single = 0, early_single = 0, updates = 0, cells_seen = 0;
    double cycles[NUM_CONFIGS] = {}, early_cycles[NUM_CONFIGS] = {};
    int simulated = 0;
    while (true) {
        if (placer.iteration % stride == 0) {
            const bool early = placer.iteration < 16;
            std::vector<Cell> by_slot(num_slots);
            std::vector<char> fast(num_slots, 0);
            for (long slot = 0; slot < num_slots; slot++) {
                const uint32_t idx = in.index[slot];
                if (idx == densrec::NO_DEPOSIT) continue;
                const Position& p = placer.next.probe_pos[slot];
                const plalgo::Window w = plalgo::footprint_window(p.x, p.y, in.footprints[idx], bins, placer.bin_w, placer.bin_h);
                Cell c{0, 0};
                for (int row = w.row_lo; row <= w.row_hi; row++)
                    for (int col = w.col_lo; col <= w.col_hi; col++) {
                        c.mask16 |= (uint16_t)(1u << (4 * (col % 4) + row % 4));
                        c.mask64 |= 1ull << (8 * (col % 8) + row % 8);
                    }
                by_slot[slot] = c;
                fast[slot] = 1;
                updates += __builtin_popcount(c.mask16);
                cells_seen += 1;
            }
            for (int k = 0; k < NUM_CONFIGS; k++) {
                const Config& c = CONFIGS[k];
                double cyc = 0;
                for (const auto& part : orders[c.readers == 4]) {
                    std::vector<uint16_t> m16;
                    std::vector<uint64_t> m64;
                    for (size_t slot : part)
                        if (fast[slot]) { if (c.banks == 16) m16.push_back(by_slot[slot].mask16); else m64.push_back(by_slot[slot].mask64); }
                    cyc += c.banks == 16 ? simulate(m16, 16, c.producers, c.writes) : simulate(m64, 64, c.producers, c.writes);
                }
                cycles[k] += cyc;
                if (early) early_cycles[k] += cyc;
            }
            total_single += (double)num_slots;
            if (early) early_single += (double)num_slots;
            simulated++;
        }
        if (max_iterations && placer.iteration >= max_iterations) break;
        placer.perform_iteration();
        if (placer.check_convergence()) break;
    }

    const double per_cell = updates / cells_seen;
    printf("whole run, %d iterations simulated; %.2f updates per cell (ceiling: 16 banks %.2f cells/cycle, 64 banks %.2f)\n",
           simulated, per_cell, 16 / per_cell, 64 / per_cell);
    printf("  %5s %9s %6s %7s %8s %8s\n", "banks", "producers", "writes", "readers", "speedup", "(it<16)");
    for (int k = 0; k < NUM_CONFIGS; k++)
        printf("  %5d %9d %6d %7d %8.2f %8.2f\n", CONFIGS[k].banks, CONFIGS[k].producers, CONFIGS[k].writes,
               CONFIGS[k].readers, total_single / cycles[k], early_single / early_cycles[k]);
    return 0;
}
