// chunk_model.cpp -- #47 M0: device cycles per chunked gradient evaluation, today vs the mailbox
// redesign (cycle_model.hpp), per design. The TODAY row must equal the legacy chunk_profile's
// "1-float" row run with --unburst 1 (make check). Meow.

#include "cycle_model.hpp"

#include <cstring>

using namespace packer;

static void print_phases(const char* label, const Phases& p, long today_total) {
    printf("  %-10s total %9ld (%5.1f%% of today) | send_reload %8ld send %8ld load %8ld receive %8ld beats %8ld "
           "return %8ld drain %8ld collect %8ld fold %8ld | update_send %7ld update_merge %7ld | mailbox %5.1f%%\n",
           label, p.total(), 100.0 * p.total() / today_total, p.send_reload, p.send, p.load, p.receive, p.beats,
           p.ret, p.drain, p.collect, p.fold_other, p.update_send, p.update_merge,
           100.0 * p.mailbox() / p.total());
}

static void usage() {
    fprintf(stderr, "usage: chunk_model [--capacity SLOTS] [--adders N] [--fm-passes N] (--netlist FILE)...\n"
                    "  --adders: merge-add adders per lane (default 1)\n");
    exit(2);
}

int main(int argc, char** argv) {
    Config cfg;
    long capacity = 1L << 20;
    int adders = 1;
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
    printf("#47 chunk_model: capacity=%ld, loop overhead=%ld, send/collect %.2f cycles/entry, %d merge adder(s) per lane, %.0f MHz\n",
           capacity, loop_overhead, unburst_cycles, adders, CLOCK_HZ / 1e6);

    for (size_t d = 0; d < designs.size(); d++) {
        const Netlist nl = read_netlist(designs[d]);
        const Chunked ch = encode_chunked(nl, cfg, capacity);
        if (!ch.fits) { printf("%s: no K fits\n", nl.name.c_str()); continue; }

        long movable = 0, max_slots = 0, tail_beats = 0, stalls = 0, shared = 0, padding = 0, max_tail_slots = 0;
        for (char m : nl.movable) movable += m;
        for (const Chunk& c : ch.chunks) {
            const ChunkFacts f = chunk_facts(c, adders);
            max_slots = std::max(max_slots, c.enc.num_slots);
            max_tail_slots = std::max(max_tail_slots, c.enc.num_slots + f.tail_extra_slot_beats * LANES);
            tail_beats += f.tail_extra_slot_beats; stalls += f.merge_stall_cycles;
            shared += f.shared_entries; padding += f.shared_padding;
        }
        printf("\n%s  movable=%ld K=%d external=%ld (%.1f%%) max_slots=%ld\n", nl.name.c_str(), movable, ch.num_chunks,
               ch.externals, 100.0 * ch.externals / movable, max_slots);
        const Phases today = model_cycles(ch, Variant::TODAY);
        print_phases("today", today, today.total());
        if (ch.num_chunks > 1) {
            print_phases("fused", model_cycles(ch, Variant::FUSED, adders), today.total());
            print_phases("fused_tail", model_cycles(ch, Variant::FUSED_TAIL, adders), today.total());
            printf("  facts: shared entries %ld (+%ld padding, charged by today's model); tail rows add %ld slot beats, "
                   "max chunk %ld slots (%s capacity); merge stalls %ld cycles\n",
                   shared, padding, tail_beats, max_tail_slots, max_tail_slots <= capacity ? "within" : "OVER", stalls);
        }
        fflush(stdout);
    }
    return 0;
}
