// chunk_profile.cpp -- #42: where a chunked design's end-to-end time goes.
//
//   1. host start-up: parse, encode_chunked as shipped (resolve, BFS cut, FM partition, builds; --fm-passes 0
//      for the bare cut), chunked_device_arrays
//   2. device cycles per gradient evaluation (one axis), per phase: cycle_model.hpp.
//   3. the same with the mailbox loops WIDENED to a 16-entry beat per cycle, using the real
//      widened layout (bring_up/mailbox_widened/mailbox_layout.hpp: bank-distinct lanes on both
//      URAM sides, collect hazard, whole-beat parcels).
//   4. ledger: start-up + ITERATIONS * GRADIENT_AXES * cycles / CLOCK_HZ.
//
// A model, not a measurement of hardware. Density / field-solve time is not included. Meow.

#include "native_netlist.hpp"
#include "cycle_model.hpp"

#include <chrono>
#include <climits>

using namespace packer;

static double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

static void print_phases(const char* label, const Phases& p) {
    const double total = (double)p.total();
    auto pct = [&](long v) { return 100.0 * v / total; };
    printf("  %-8s total %9ld | send_reload %8ld send %8ld load %8ld receive %8ld beats %8ld return %8ld drain %8ld collect %8ld fold %8ld | mailbox %5.1f%%  beats %5.1f%%\n",
           label, p.total(), p.send_reload, p.send, p.load, p.receive, p.beats, p.ret, p.drain, p.collect, p.fold_other,
           pct(p.mailbox()), pct(p.beats));
}

static void usage() {
    fprintf(stderr,
        "usage: chunk_profile [--capacity SLOTS] [--force-k K] [--overhead CYCLES] [--unburst CYCLES] [--small-only] [--fm-passes N]\n"
        "                     (--bookshelf DIR NAME | --def FILE NAME)...\n"
        "  --force-k: the bare cut at exactly K, capacity unchecked (what-if)\n"
        "  --fm-passes: encode_chunked's FM passes (default 1; 0 = the bare cut)\n");
    exit(2);
}

int main(int argc, char** argv) {
    Config cfg;
    long capacity = 1L << 20;
    int force_k = 0;
    std::vector<std::pair<std::string, std::string>> designs;
    std::vector<char> is_def;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() { if (i + 1 >= argc) usage(); return std::string(argv[++i]); };
        if      (arg == "--capacity") capacity = std::stol(next());
        else if (arg == "--force-k")  force_k = std::stoi(next());
        else if (arg == "--overhead") loop_overhead = std::stol(next());
        else if (arg == "--unburst")  unburst_cycles = std::stod(next());
        else if (arg == "--small-only") cfg.large_nets = false;
        else if (arg == "--fm-passes") cfg.partition_fm_passes = std::stoi(next());
        else if (arg == "--bookshelf" || arg == "--def") {
            std::string path = next(), name = next();
            designs.emplace_back(path, name); is_def.push_back(arg == "--def");
        } else usage();
    }
    if (designs.empty()) usage();
    printf("#42 chunk_profile: capacity=%ld, loop overhead=%ld cycles, 1-float send/collect %.2f cycles/entry, %.0f MHz, %d iterations x %d axes\n",
           capacity, loop_overhead, unburst_cycles, CLOCK_HZ / 1e6, ITERATIONS, GRADIENT_AXES);

    for (size_t d = 0; d < designs.size(); d++) {
        auto t = std::chrono::steady_clock::now();
        const Netlist nl = is_def[d] ? read_def_native(designs[d].first, designs[d].second)
                                     : read_bookshelf_native(designs[d].first, designs[d].second);
        const double parse_s = seconds_since(t);

        // encode_chunked as shipped (resolve, BFS cut, FM partition, builds), or with --force-k the
        // bare cut at exactly K, capacity unchecked. Meow.
        Chunked ch;
        t = std::chrono::steady_clock::now();
        if (force_k) {
            ch.capacity = LONG_MAX;
            resolve_pin_nodes(nl, ch.global);
            ch.fits = build_chunks(ch, locality_order(ch.global), force_k, cfg);
        } else {
            ch = encode_chunked(nl, cfg, capacity);
        }
        const double encode_s = seconds_since(t);
        if (!ch.fits) { printf("%s: no K fits\n", nl.name.c_str()); continue; }

        t = std::chrono::steady_clock::now();
        std::vector<float> zero_pos(ch.global.kind.size(), 0.0f);   // positions do not change the layout cost. Meow.
        for (int axis = 0; axis < 2; axis++) { volatile size_t sink = chunked_device_arrays(ch, axis, zero_pos).records.size(); (void)sink; }
        const double device_arrays_s = seconds_since(t);
        const double startup_s = parse_s + encode_s + device_arrays_s;

        long max_slots = 0, movable = 0;
        for (const Chunk& c : ch.chunks) max_slots = std::max(max_slots, c.enc.num_slots);
        for (char m : nl.movable) movable += m;
        printf("\n%s  movable=%ld K=%d external=%ld (%.1f%%) max_slots=%ld\n", nl.name.c_str(), movable, ch.num_chunks,
               ch.externals, 100.0 * ch.externals / movable, max_slots);
        printf("  host s   total %6.1f | parse %5.1f encode_chunked %5.1f (resolve + BFS cut + FM %d pass + builds) device_arrays %5.1f\n",
               startup_s, parse_s, encode_s, force_k ? 0 : cfg.partition_fm_passes, device_arrays_s);

        const Phases now = model_cycles(ch, nullptr);
        print_phases("1-float", now);
        double widened_device_s = 0, layout_s = 0;
        if (ch.num_chunks > 1) {
            t = std::chrono::steady_clock::now();
            const WideMailbox wide = build_wide_mailbox(ch, cfg);
            layout_s = seconds_since(t);
            const long violations = check_wide_mailbox(ch, wide, cfg);
            const Phases widened = model_cycles(ch, &wide);
            print_phases("widened", widened);
            printf("  widening: %ld entries -> %ld beats (%.2f%% lane use, %ld hazard beats), layout %.1fs, %ld violations%s\n",
                   wide.entries, wide.mailbox_beats, 100.0 * wide.entries / (wide.mailbox_beats * (double)LANES), wide.hazard_beats,
                   layout_s, violations, violations ? "  FAIL" : "");
            widened_device_s = (double)ITERATIONS * GRADIENT_AXES * widened.total() / CLOCK_HZ;
        }
        const double device_s = (double)ITERATIONS * GRADIENT_AXES * now.total() / CLOCK_HZ;
        printf("  ledger   start-up %.1fs + gradient device %.1fs = %.1fs", startup_s, device_s, startup_s + device_s);
        if (ch.num_chunks > 1)
            printf("   | widened: %.1fs + %.1fs = %.1fs", startup_s + layout_s, widened_device_s, startup_s + layout_s + widened_device_s);
        printf("\n");
        fflush(stdout);
    }
    return 0;
}
