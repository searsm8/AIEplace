// lane_model -- #48: how well 8 lanes of bin_density_CU would share the deposit, on real positions, at
// every iteration of a sw_flat run.
//
//   lane_model <config.toml> [max_iterations, 0 = to the stop] [stride: simulate every Nth iteration]
//
// Each lane owns some grid rows, with its own 16 banks (bank = 4*(col%4) + row%4), and deposits only into
// them, so a slot becomes one work item per lane whose rows its footprint touches (one or two). Row maps:
//   bands     lane = row / (bins/8): equal contiguous bands
//   strip4/8  lane = (row/N) % 8: N-row strips dealt round-robin
//   graded    contiguous bands, thinner in the middle (Mark): heights 3 : 2.5 : 1.5 : 1 : 1 : 1.5 : 2.5 : 3
//   mid-strip the outer quarters as contiguous bands (2 lanes each), the middle half dealt in 4-row
//             strips over the 4 middle lanes
// URAMs: each lane's 16 banks hold (bins/4) * (its rows / 4) words; a URAM holds 8K x 36 b. At 1024.
//
// Cycles per evaluation (the fast deposit; slow list, load, reduce and drain are separate), simulated:
//   fifo      a FIFO per lane (depth 32); the dispatcher writes <= 4 items into a FIFO per cycle and moves
//             on to the next beat once its items are written; a lane takes one item per cycle
//   read4     the same, FIFO depth 128, the dispatcher reading the stream at 4 places in turn
//   +pair     read4, and a lane takes the NEXT item too when its banks do not overlap the first's: two
//             cells deposited in one cycle, each bank still doing one read-add-write
//   bound     the busiest lane's total items (perfect buffering, one item per lane per cycle)
// Speedup = (slots, one per cycle, as the CU is today) / cycles. The overflow barrier (cells, then
// fillers) drains the lanes once. Positions are sw_flat's own, so this is the stream the CU would see. Meow.

#include "density_inputs.hpp"
#include "modules/bin_density_CU.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace sw_flat;

namespace {

constexpr int LANES = 8, FIFO_WRITES = 4, READ_POINTS = 4;
constexpr int NUM_MAPS = 5;
const char* MAP_NAME[NUM_MAPS] = {"bands", "strip4", "strip8", "graded", "mid-strip"};
const double GRADED[LANES] = {3, 2.5, 1.5, 1, 1, 1.5, 2.5, 3};   // relative band heights, sum 16

// lane_of[map][row] for one grid size. Meow.
std::vector<std::vector<int>> make_lane_maps(int bins) {
    std::vector<std::vector<int>> lane(NUM_MAPS, std::vector<int>(bins));
    std::vector<int> graded_end(LANES);
    double cum = 0;
    for (int l = 0; l < LANES; l++) { cum += GRADED[l]; graded_end[l] = (int)std::lround(cum / 16.0 * bins / 4) * 4; }
    for (int r = 0; r < bins; r++) {
        lane[0][r] = r / (bins / LANES);
        lane[1][r] = (r / 4) % LANES;
        lane[2][r] = (r / 8) % LANES;
        lane[3][r] = (int)(std::upper_bound(graded_end.begin(), graded_end.end(), r) - graded_end.begin());
        const int q = bins / 4;
        lane[4][r] = r < q ? r / (q / 2) : r >= 3 * q ? 6 + (r - 3 * q) / (q / 2) : 2 + ((r - q) / 4) % 4;
    }
    return lane;
}

int urams_at_1024(const std::vector<int>& lane_of_row) {   // the map scaled to 1024 rows
    std::array<int, LANES> rows{};
    const int bins = (int)lane_of_row.size();
    for (int r = 0; r < bins; r++) rows[lane_of_row[r]]++;
    int urams = 0;
    for (int l = 0; l < LANES; l++) {
        const long words = (1024L / 4) * (rows[l] * (1024L / bins) / 4);
        urams += 16 * (int)((words + 8191) / 8192);
    }
    return urams;
}

struct Item { uint8_t lane; uint16_t banks; };
using BeatItems = std::vector<std::vector<Item>>;   // per beat, its items in slot order

// Beats [first, last) in the order the dispatcher reads them: one reader, or READ_POINTS in turn. Meow.
std::vector<size_t> read_order(size_t first, size_t last, int readers) {
    std::vector<size_t> order;
    const size_t n = last - first, per = (n + readers - 1) / readers;
    for (size_t k = 0; k < per; k++)
        for (int r = 0; r < readers; r++)
            if (r * per + k < n) order.push_back(first + r * per + k);
    return order;
}

// The FIFO dispatcher and the lanes, cycle by cycle, over the beats in `order`. Meow.
double simulate(const BeatItems& beats, const std::vector<size_t>& order, int depth, bool pair) {
    std::array<std::vector<uint16_t>, LANES> queue;   // every item a lane will get, in arrival order
    for (size_t b : order) for (const Item& it : beats[b]) queue[it.lane].push_back(it.banks);
    std::array<size_t, LANES> pushed{}, popped{};
    std::array<int, LANES> pending{};
    long cycles = 0;
    size_t next = 0;
    bool loaded = false;
    while (true) {
        for (int l = 0; l < LANES; l++) {
            if (popped[l] == pushed[l]) continue;
            const uint16_t first = queue[l][popped[l]++];
            if (pair && popped[l] < pushed[l] && (queue[l][popped[l]] & first) == 0) popped[l]++;
        }
        if (next < order.size()) {
            if (!loaded) {
                pending.fill(0);
                for (const Item& it : beats[order[next]]) pending[it.lane]++;
                loaded = true;
            }
            bool done = true;
            for (int l = 0; l < LANES; l++) {
                const int room = depth - (int)(pushed[l] - popped[l]);
                const int write = std::min({pending[l], FIFO_WRITES, room});
                pushed[l] += write;
                pending[l] -= write;
                done &= pending[l] == 0;
            }
            if (done) { next++; loaded = false; }
        }
        cycles++;
        bool idle = next == order.size();
        for (int l = 0; l < LANES; l++) idle &= popped[l] == pushed[l];
        if (idle) return (double)cycles;
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: lane_model <config.toml> [max_iterations] [stride]\n"); return 2; }
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
    const long num_slots = placer.layout.num_slots, num_beats = num_slots / pinrec::LANES;
    const size_t filler_beat = (size_t)(placer.layout.first_filler_slot / pinrec::LANES);
    const auto lane_of = make_lane_maps(bins);
    long fast_slots = 0;
    for (uint32_t idx : in.index) fast_slots += idx != densrec::NO_DEPOSIT;
    printf("%s grid %d, 8 lanes: %ld slots in %ld beats, %ld fast, %zu slow; simulated every %d iterations\n",
           placer.design.name.c_str(), bins, num_slots, num_beats, fast_slots, in.slow.size(), stride);

    const std::vector<size_t> one_reader_cells = read_order(0, filler_beat, 1), one_reader_fill = read_order(filler_beat, num_beats, 1);
    const std::vector<size_t> four_cells = read_order(0, filler_beat, READ_POINTS), four_fill = read_order(filler_beat, num_beats, READ_POINTS);
    double total_single = 0, items_total[NUM_MAPS] = {}, fifo[NUM_MAPS] = {}, read4[NUM_MAPS] = {}, paired[NUM_MAPS] = {}, bound[NUM_MAPS] = {};
    double early_single = 0, early_paired[NUM_MAPS] = {}, early_bound[NUM_MAPS] = {};
    BeatItems beats(num_beats);
    int simulated = 0;
    while (true) {
        if (placer.iteration % stride == 0) {
            const bool early = placer.iteration < 16;
            for (int m = 0; m < NUM_MAPS; m++) {
                for (auto& b : beats) b.clear();
                std::array<long, LANES> load{};
                for (long slot = 0; slot < num_slots; slot++) {
                    const uint32_t idx = in.index[slot];
                    if (idx == densrec::NO_DEPOSIT) continue;
                    const Position& p = placer.next.probe_pos[slot];
                    const plalgo::Window w = plalgo::footprint_window(p.x, p.y, in.footprints[idx], bins, placer.bin_w, placer.bin_h);
                    // One item per lane touched; its banks are the window's (col, row) pairs in that lane's rows. Meow.
                    std::array<uint16_t, LANES> banks{};
                    for (int row = w.row_lo; row <= w.row_hi; row++)
                        for (int col = w.col_lo; col <= w.col_hi; col++)
                            banks[lane_of[m][row]] |= (uint16_t)(1u << (4 * (col % 4) + row % 4));
                    for (int l = 0; l < LANES; l++)
                        if (banks[l]) { beats[slot / pinrec::LANES].push_back(Item{(uint8_t)l, banks[l]}); load[l]++; }
                }
                long items = 0;
                for (long n : load) items += n;
                items_total[m] += (double)items / fast_slots;
                const double b = (double)*std::max_element(load.begin(), load.end());
                const double f = simulate(beats, one_reader_cells, 32, false) + simulate(beats, one_reader_fill, 32, false);
                const double r = simulate(beats, four_cells, 128, false) + simulate(beats, four_fill, 128, false);
                const double pr = simulate(beats, four_cells, 128, true) + simulate(beats, four_fill, 128, true);
                bound[m] += b; fifo[m] += f; read4[m] += r; paired[m] += pr;
                if (early) { early_paired[m] += pr; early_bound[m] += b; }
            }
            total_single += (double)num_slots;
            if (placer.iteration < 16) early_single += (double)num_slots;
            simulated++;
        }
        if (max_iterations && placer.iteration >= max_iterations) break;
        placer.perform_iteration();
        if (placer.check_convergence()) break;
    }

    printf("whole run, %d iterations simulated; speedup over one slot per cycle (iterations 0-15 alone in brackets):\n", simulated);
    printf("  %-9s %6s %7s %6s %6s %13s %13s\n", "map", "URAMs", "items", "fifo", "read4", "+pair", "bound");
    for (int m = 0; m < NUM_MAPS; m++)
        printf("  %-9s %6d %6.2fx %6.2f %6.2f %6.2f (%4.2f) %6.2f (%4.2f)\n", MAP_NAME[m], urams_at_1024(lane_of[m]),
               items_total[m] / simulated, total_single / fifo[m], total_single / read4[m], total_single / paired[m],
               early_single / early_paired[m], total_single / bound[m], early_single / early_bound[m]);
    return 0;
}
