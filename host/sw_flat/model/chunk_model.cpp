// chunk_model.cpp -- #47: device cycles per chunked gradient evaluation, today's device (B3) vs the
// exchange fused into iteration_update (B4), per design (cycle_model.hpp). Meow.

#include "cycle_model.hpp"

#include <cstring>

using namespace packer;

static void print_phases(const char* label, const Phases& p, long today_total) {
    printf("  %-6s total %9ld (%5.1f%% of today) | send_reload %8ld send %8ld load %8ld beats %8ld drain %8ld "
           "merge %8ld fold %8ld | update_send %7ld update_merge %7ld | exchange %5.1f%%\n",
           label, p.total(), 100.0 * p.total() / today_total, p.send_reload, p.send, p.load, p.beats, p.drain,
           p.merge, p.fold_other, p.update_send, p.update_merge, 100.0 * p.exchange() / p.total());
}

static void usage() {
    fprintf(stderr, "usage: chunk_model [--capacity SLOTS] [--adders N] [--fm-passes N] (--netlist FILE)...\n"
                    "  --adders: B4 merge-add adders per lane (default 2)\n");
    exit(2);
}

int main(int argc, char** argv) {
    Config cfg;
    long capacity = 1L << 20;
    int adders = 2;
    std::vector<std::string> designs;   // netlist files: make -C host/sw_flat netlists
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        auto next = [&]() { if (i + 1 >= argc) usage(); return std::string(argv[++i]); };
        if      (arg == "--capacity")  capacity = std::stol(next());
        else if (arg == "--adders")    adders = std::stoi(next());
        else if (arg == "--fm-passes") cfg.partition_fm_passes = std::stoi(next());
        else if (arg == "--netlist")   designs.push_back(next());
        else usage();
    }
    if (designs.empty()) usage();
    printf("#47 chunk_model: capacity=%ld, loop overhead=%ld, %d merge adder(s) per lane (B4), %.0f MHz\n",
           capacity, loop_overhead, adders, CLOCK_HZ / 1e6);

    for (size_t d = 0; d < designs.size(); d++) {
        const Netlist nl = read_netlist(designs[d]);
        const Chunked ch = encode_chunked(nl, cfg, capacity);
        if (!ch.fits) { printf("%s: no K fits\n", nl.name.c_str()); continue; }
        long movable = 0, max_slots = 0, blocks = 0;
        for (char m : nl.movable) movable += m;
        for (const Chunk& c : ch.chunks) { max_slots = std::max(max_slots, c.enc.num_slots); blocks += (long)c.tail_blocks.size(); }
        printf("\n%s  movable=%ld K=%d external=%ld (%.1f%%) max_slots=%ld tail_blocks=%ld\n", nl.name.c_str(), movable,
               ch.num_chunks, ch.externals, 100.0 * ch.externals / movable, max_slots, blocks);
        const Phases today = model_cycles(ch, Variant::TODAY);
        print_phases("today", today, today.total());
        if (ch.num_chunks > 1) print_phases("fused", model_cycles(ch, Variant::FUSED, adders), today.total());
        fflush(stdout);
    }
    return 0;
}
