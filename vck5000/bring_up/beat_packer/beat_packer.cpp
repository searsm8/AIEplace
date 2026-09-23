// beat_packer.cpp -- CLI over beat_packer.hpp: encode real netlists into the static pin-record
// stream (#41), verify each by decoding it back, and report packing, bit and URAM figures.
// See README.md for the protocol. Meow.

#include "beat_packer.hpp"

#include <chrono>

using namespace packer;

static void report(const Netlist& nl, const Encoded& enc, double seconds, int failures) {
    long movable = 0, pins_in_scope = 0, pins_17_100 = 0, pins_all = 0, bubbles = 0, kind_count[5] = {};
    for (char m : nl.movable) movable += m;
    for (size_t node = 0; node < enc.kind.size(); node++) kind_count[enc.kind[node]] += enc.node_slot[node] >= 0;
    for (const auto& net : nl.nets) {
        pins_all += net.size();
        if (in_scope(net)) pins_in_scope += net.size(); else pins_17_100 += net.size();
    }
    for (int b : enc.issue) bubbles += (b == BUBBLE);
    const double efficiency = (double)enc.ideal_beats / enc.issue.size();
    const long uram = enc.uram_grad + enc.uram_pos;
    const int node_bits = bits_for(enc.max_slot + 1);   // +1: the EMPTY value above every real slot
    printf("%-22s %8ld %6ld %6ld %7ld %9ld %6.2f%% %6.2f%% %6ld %7.2f%% %5zu %5zu %2d+%-2d=%2d %4ld%s %6.1fs %s\n",
           nl.name.c_str(), movable, kind_count[MACRO], kind_count[MACRO_PIN], kind_count[FIXED_PIN], pins_in_scope,
           100.0 * pins_17_100 / pins_all,
           100.0 * ((long)enc.beats.size() - enc.ideal_beats) / enc.ideal_beats, bubbles, 100.0 * efficiency,
           enc.offset_table[0].size(), enc.offset_table[1].size(),
           node_bits, enc.offset_bits, node_bits + enc.offset_bits,
           uram, uram > VC1902_URAMS ? "!" : " ", seconds, failures ? "FAIL" : "ok");
}

static void usage() {
    fprintf(stderr,
        "usage: beat_packer [--hazard H] [--window W] [--seed S] [--repair N]\n"
        "                   (--bookshelf DIR NAME | --def FILE NAME)...\n");
    exit(2);
}

int main(int argc, char** argv) {
    Config cfg;
    std::vector<std::pair<std::string, std::string>> designs;   // (path, name)
    std::vector<char> is_def;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() { if (i + 1 >= argc) usage(); return std::string(argv[++i]); };
        if      (arg == "--hazard") cfg.hazard = std::stoi(next());
        else if (arg == "--window") cfg.window = std::stoi(next());
        else if (arg == "--seed")   cfg.seed   = (unsigned)std::stoul(next());
        else if (arg == "--repair") cfg.repair_passes = std::stoi(next());
        else if (arg == "--bookshelf" || arg == "--def") {
            std::string path = next(), name = next();
            designs.emplace_back(path, name); is_def.push_back(arg == "--def");
        } else usage();
    }
    if (designs.empty()) usage();

    printf("banks=%d hazard=%d window=%d seed=%u   URAM = grad(movable) + pos(all slots), %d floats/word, of %d\n",
           BANKS, cfg.hazard, cfg.window, cfg.seed, FLOATS_PER_WORD, VC1902_URAMS);
    printf("%-22s %8s %6s %6s %7s %9s %7s %7s %6s %8s %5s %5s %8s %4s  %7s\n", "design", "movable", "macro",
           "m_pins", "f_pins", "pins<=16", "pin>16", "packlos", "bubble", "effic", "offx", "offy", "bits n+o", "URAM", "time");
    int total_failures = 0;
    for (size_t d = 0; d < designs.size(); d++) {
        const auto start = std::chrono::steady_clock::now();
        const Netlist nl = is_def[d] ? read_def(designs[d].first, designs[d].second)
                                     : read_bookshelf(designs[d].first, designs[d].second);
        const Encoded enc = encode_netlist(nl, cfg);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const int failures = check(nl, enc, cfg);
        total_failures += failures;
        report(nl, enc, seconds, failures);
        fflush(stdout);
    }
    return total_failures ? 1 : 0;
}
