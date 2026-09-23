// large_net_stats.cpp -- cost model for 17..100-pin nets on the record pipeline (#41 / #40 plan).
//
// For each design: the small-net stream as encoded today, and what a separate large-net chunk
// stream would cost under the three-pass proposal (bbox / sums / combine, each II=1 over chunk
// beats, nets processed in groups of G with one pipeline drain per pass per group):
//
//   chunks   sum over large nets of ceil(distinct nodes / 16)  (a node's repeated pins share a lane run)
//   cycles   small beats + 3 * chunks + 3 * drain * groups
//
// `drain` is the gradient beat-loop depth from C-synthesis (84). This is a model, not a measurement
// of hardware; it exists to decide whether the proposal is worth building. Meow.

#include "beat_packer.hpp"

#include <chrono>

using namespace packer;

int main(int argc, char** argv) {
    const int group = 1024, drain = 84;
    printf("large nets: groups of %d nets, %d-cycle drain per pass per group\n", group, drain);
    printf("%-22s %9s %9s %7s %9s %8s %9s %11s %7s\n", "design", "nets>16", "pins>16", "pin%", "chunks",
           "small_bt", "cyc_small", "cyc_total", "ratio");
    for (int i = 1; i + 2 < argc + 1; ) {
        std::string kind = argv[i], path = argv[i + 1], name = argv[i + 2];
        i += 3;
        const Netlist nl = kind == "--def" ? read_def(path, name) : read_bookshelf(path, name);
        const Encoded enc = encode_netlist(nl, Config());
        long large_nets = 0, large_pins = 0, all_pins = 0, chunks = 0;
        for (size_t n = 0; n < nl.nets.size(); n++) {
            all_pins += nl.nets[n].size();
            if (in_scope(nl.nets[n])) continue;
            large_nets++;
            large_pins += nl.nets[n].size();
            chunks += ((long)enc.unique_nodes[n].size() + LANES - 1) / LANES;
        }
        const long small_beats = (long)enc.issue.size();
        const long groups = (large_nets + group - 1) / group;
        const long total = small_beats + 3 * chunks + 3L * drain * groups;
        printf("%-22s %9ld %9ld %6.1f%% %9ld %8ld %9ld %11ld %6.2fx\n", nl.name.c_str(), large_nets, large_pins,
               100.0 * large_pins / all_pins, chunks, small_beats, small_beats, total, (double)total / small_beats);
        fflush(stdout);
    }
    return 0;
}
