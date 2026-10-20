// lane_model -- #48: how well LANES lanes of bin_density_CU would share the deposit, on real positions,
// at every iteration of a sw_flat run.
//
//   lane_model <config.toml> [max_iterations, 0 = to the stop]
//
// Each lane owns some grid rows and deposits only into them (with its own 16 banks), so a slot goes to
// every lane whose rows its footprint touches. Two ways to give out the rows:
//   bands    lane = row / (bins / LANES): contiguous row bands
//   stripN   lane = (row / N) % LANES: N-row strips dealt round-robin, so a spatial cluster spreads out;
//            N = 4, 8, 16, 32 (taller strips: fewer footprints straddle two lanes, but less spreading)
// A fast footprint spans <= 4 rows, so it touches at most 2 lanes either way.
//
// Cycles per evaluation (the fast deposit only; the slow list, load, reduce and drain are separate):
//   single   today's CU: one slot per cycle, empty slots included (num_slot_beats * 16)
//   beat     a dispatcher that hands one 16-slot beat to the lanes and waits until every lane has taken
//            its share, one per cycle: sum over beats of max(1, busiest lane)
//   fifo     each lane has a FIFO of FIFO_DEPTH; the dispatcher writes at most FIFO_WRITES entries into
//            one FIFO per cycle and moves on to the next beat once its share is written; lanes pop one
//            per cycle. The overflow barrier (cells, then fillers) drains the lanes once. Simulated.
//   bound    the busiest lane's total work: what perfect buffering would give
// Then two remedies for the FIFO's shortfall, on strip4 and strip8 only (the fifo sweep is the slow part):
//   deep     FIFO depth 128 instead of 32
//   readN    the dispatcher reads the stream at READ_POINTS places at once, one beat from each in turn,
//            so consecutive beats come from different parts of the die (cells and fillers separately)
//   both     deep + read4
// Printed per checkpoint iteration, and summed over the whole run (the number that matters).
// Positions are sw_flat's own (pl+Bprime+fixed config), so this is the stream the CU would see. Meow.

#include "density_inputs.hpp"
#include "modules/bin_density_CU.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace sw_flat;

namespace {

constexpr int LANES = 8;
constexpr int FIFO_DEPTH = 32, DEEP_FIFO_DEPTH = 128, READ_POINTS = 4;
constexpr int FIFO_WRITES = 4;   // entries one lane's FIFO can accept per cycle
constexpr int NUM_MAPS = 5;
const char* MAP_NAME[NUM_MAPS] = {"bands", "strip4", "strip8", "strip16", "strip32"};
constexpr int STRIP_ROWS[NUM_MAPS] = {0, 4, 8, 16, 32};   // 0: contiguous bands

int lane_of(int map, int row, int bins) {
    return STRIP_ROWS[map] == 0 ? row / (bins / LANES) : (row / STRIP_ROWS[map]) % LANES;
}

struct Cycles { double beat = 0, fifo = 0, bound = 0; };

// One beat's share per lane, for every beat of the stream. Meow.
using BeatShares = std::vector<std::array<uint8_t, LANES>>;

double beat_sync(const BeatShares& shares) {
    double cycles = 0;
    for (const auto& s : shares) cycles += std::max<int>(1, *std::max_element(s.begin(), s.end()));
    return cycles;
}

// The FIFO dispatcher, cycle by cycle, over beats [first, last); returns when every lane has drained. Meow.
double fifo_sim(const BeatShares& shares, size_t first, size_t last, int depth = FIFO_DEPTH) {
    std::array<int, LANES> occupancy{}, pending{};
    long cycles = 0;
    size_t beat = first;
    bool loaded = false;
    while (true) {
        for (int l = 0; l < LANES; l++) if (occupancy[l] > 0) occupancy[l]--;   // each lane pops one
        if (beat < last) {
            if (!loaded) { for (int l = 0; l < LANES; l++) pending[l] = shares[beat][l]; loaded = true; }
            bool done = true;
            for (int l = 0; l < LANES; l++) {
                const int room = depth - occupancy[l];
                const int write = std::min({pending[l], FIFO_WRITES, room});
                occupancy[l] += write;
                pending[l] -= write;
                done &= pending[l] == 0;
            }
            if (done) { beat++; loaded = false; }
        }
        cycles++;
        bool idle = beat == last;
        for (int l = 0; l < LANES; l++) idle &= occupancy[l] == 0;
        if (idle) return (double)cycles;
    }
}

// Beats [first, last) as READ_POINTS readers see them: reader r starts r/READ_POINTS of the way in, and
// the dispatcher takes one beat from each reader in turn. Meow.
BeatShares interleaved(const BeatShares& shares, size_t first, size_t last) {
    BeatShares out;
    const size_t n = last - first, per = (n + READ_POINTS - 1) / READ_POINTS;
    for (size_t k = 0; k < per; k++)
        for (int r = 0; r < READ_POINTS; r++)
            if (r * per + k < n) out.push_back(shares[first + r * per + k]);
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: lane_model <config.toml> [max_iterations]\n"); return 2; }
    const int max_iterations = argc > 2 ? atoi(argv[2]) : 0;
    const int remedy_stride = argc > 3 ? atoi(argv[3]) : 1;   // run the remedy sims every Nth iteration
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
    const long num_slots = placer.layout.num_slots, num_beats = num_slots / pinrec::LANES;
    const size_t filler_beat = (size_t)(placer.layout.first_filler_slot / pinrec::LANES);
    long fast_slots = 0;
    for (uint32_t idx : in.index) fast_slots += idx != densrec::NO_DEPOSIT;
    printf("%s grid %d lanes %d: %ld slots in %ld beats, %ld fast, %zu slow; fifo depth %d, %d writes/cycle\n",
           placer.design.name.c_str(), placer.bins, LANES, num_slots, num_beats, fast_slots, in.slow.size(), FIFO_DEPTH, FIFO_WRITES);
    printf("%5s %10s", "iter", "single");
    for (int m = 0; m < NUM_MAPS; m++) printf("  %7s: 2lane  fifo bound", MAP_NAME[m]);
    printf("   (fifo, bound: speedup over single)\n");

    double run_single = 0, run_two_lane = 0, sampled_single = 0;
    constexpr int REMEDY_MAPS[2] = {1, 2};   // strip4, strip8
    const char* REMEDY_NAME[4] = {"fifo32", "deep", "read4", "both"};
    double run_remedy[2][4] = {};
    Cycles run[NUM_MAPS];
    BeatShares shares[NUM_MAPS];
    for (int m = 0; m < NUM_MAPS; m++) shares[m].assign(num_beats, {});
    int evaluations = 0;
    while (true) {
        // The positions bin_density_CU would see now: the deposit just done read probe_pos. Meow.
        long two_lane[NUM_MAPS] = {};
        for (int m = 0; m < NUM_MAPS; m++) std::fill(shares[m].begin(), shares[m].end(), std::array<uint8_t, LANES>{});
        for (long slot = 0; slot < num_slots; slot++) {
            const uint32_t idx = in.index[slot];
            if (idx == densrec::NO_DEPOSIT) continue;
            const Position& p = placer.next.probe_pos[slot];
            const plalgo::Window w = plalgo::footprint_window(p.x, p.y, in.footprints[idx], placer.bins, placer.bin_w, placer.bin_h);
            for (int m = 0; m < NUM_MAPS; m++) {
                const int lo = lane_of(m, w.row_lo, placer.bins), hi = lane_of(m, w.row_hi, placer.bins);
                shares[m][slot / pinrec::LANES][lo]++;
                if (hi != lo) { shares[m][slot / pinrec::LANES][hi]++; two_lane[m]++; }
            }
        }
        const double single = (double)num_slots;
        Cycles now[NUM_MAPS];
        for (int m = 0; m < NUM_MAPS; m++) {
            std::array<long, LANES> load{};
            for (const auto& s : shares[m]) for (int l = 0; l < LANES; l++) load[l] += s[l];
            now[m].bound = (double)*std::max_element(load.begin(), load.end());
            now[m].beat = beat_sync(shares[m]);
            now[m].fifo = fifo_sim(shares[m], 0, filler_beat) + fifo_sim(shares[m], filler_beat, shares[m].size());
            run[m].beat += now[m].beat; run[m].fifo += now[m].fifo; run[m].bound += now[m].bound;
        }
        run_single += single;
        if (placer.iteration % remedy_stride == 0) {
            sampled_single += single;
            for (int k = 0; k < 2; k++) {
                const BeatShares& sh = shares[REMEDY_MAPS[k]];
                const BeatShares cells = interleaved(sh, 0, filler_beat), fill = interleaved(sh, filler_beat, sh.size());
                run_remedy[k][0] += fifo_sim(sh, 0, filler_beat) + fifo_sim(sh, filler_beat, sh.size());
                run_remedy[k][1] += fifo_sim(sh, 0, filler_beat, DEEP_FIFO_DEPTH) + fifo_sim(sh, filler_beat, sh.size(), DEEP_FIFO_DEPTH);
                run_remedy[k][2] += fifo_sim(cells, 0, cells.size()) + fifo_sim(fill, 0, fill.size());
                run_remedy[k][3] += fifo_sim(cells, 0, cells.size(), DEEP_FIFO_DEPTH) + fifo_sim(fill, 0, fill.size(), DEEP_FIFO_DEPTH);
            }
        }
        run_two_lane += (double)two_lane[1];
        evaluations++;

        const int it = placer.iteration;
        if (it <= 8 || (it & (it - 1)) == 0 || it % 100 == 0) {
            printf("%5d %10.0f", it, single);
            for (int m = 0; m < NUM_MAPS; m++)
                printf("  %7s: %4.0f%% %5.2f %5.2f", "", 100.0 * two_lane[m] / fast_slots, single / now[m].fifo, single / now[m].bound);
            printf("\n");
        }
        if (max_iterations && placer.iteration >= max_iterations) break;
        placer.perform_iteration();
        if (placer.check_convergence()) break;
    }

    printf("whole run, %d evaluations (strip4 put %.1f%% of fast slots in two lanes):\n", evaluations, 100.0 * run_two_lane / (fast_slots * (double)evaluations));
    for (int m = 0; m < NUM_MAPS; m++)
        printf("  %-7s speedup over single: beat %.2f  fifo %.2f  bound %.2f\n", MAP_NAME[m], run_single / run[m].beat,
               run_single / run[m].fifo, run_single / run[m].bound);
    printf("remedies (every %d-th iteration), speedup over single:\n", remedy_stride);
    for (int k = 0; k < 2; k++) {
        printf("  %-7s", MAP_NAME[REMEDY_MAPS[k]]);
        for (int v = 0; v < 4; v++) printf("  %s %.2f", REMEDY_NAME[v], sampled_single / run_remedy[k][v]);
        printf("\n");
    }
    return 0;
}
