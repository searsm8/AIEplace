// chunk_profile.cpp -- #42: where a chunked design's end-to-end time goes.
//
//   1. host start-up, per stage: parse, resolve_pin_nodes, locality_order, every build_chunks
//      attempt (a failed K is paid in full before the next K starts), chunked_device_arrays
//   2. device cycles per gradient evaluation (one axis), per phase of hpwl_gradient_computer_v2,
//      from the descriptor trip counts. Every loop is II=1, so a phase costs its trip count plus
//      loop_overhead per loop invocation (pipeline fill + m_axi latency). EXCEPT the 1-float send
//      write and collect read of the mailbox: neither is inferred as a burst (send's write sits
//      behind `if (slot < 0) continue`), so each waits on DDR latency with 16 requests in flight.
//      Fitted to two RTL co-sims of hpwl_gradient_computer_v2 (2026-10-02; K=10 / 953 external ->
//      62,573 cycles, K=2 / 2,275 external -> 37,373): loop_overhead 165 cycles, send/collect 4.26
//      cycles per entry (= the co-sim AXI model's 64-cycle latency / 16 outstanding). Real DDR
//      latency is likely higher, so 4.26 is a floor. On the real designs invocations are < 1%.
//   3. the same with the mailbox loops WIDENED to a 16-entry beat per cycle, using the real
//      widened layout (bring_up/mailbox_widened/mailbox_layout.hpp: bank-distinct lanes on both
//      URAM sides, collect hazard, whole-beat parcels).
//   4. ledger: start-up + ITERATIONS * GRADIENT_AXES * cycles / CLOCK_HZ.
//
// A model, not a measurement of hardware. Density / field-solve time is not included. Meow.

#include "beat_packer.hpp"
#include "mailbox_layout.hpp"

#include <chrono>
#include <climits>

using namespace packer;

constexpr double CLOCK_HZ      = 300e6;   // Mark, 2026-10-02. v2 C-synth does NOT yet meet 3.33 ns (negative slack). Meow.
constexpr int    ITERATIONS    = 1000;
constexpr int    GRADIENT_AXES = 2;
constexpr long   EXP_LUT_ENTRIES = 1024;  // GRAD_LUT_MAX, an upper bound on the exp LUT the prologue loads. Meow.
static long      loop_overhead = 165;     // cycles per loop invocation; see 2. above. Meow.
static double    unburst_cycles = 4.26;   // per 1-float send / collect entry; see 2. above. Meow.

static double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct Phases {
    long prologue = 0, send_reload = 0, send = 0, load = 0, receive = 0, beats = 0, ret = 0, drain = 0, collect = 0, fold_other = 0;
    long mailbox() const { return send + receive + ret + collect; }
    long total() const { return prologue + send_reload + send + load + receive + beats + ret + drain + collect + fold_other; }
};

static Phases model_cycles(const Chunked& ch, const WideMailbox* wide) {
    Phases p;
    const bool chunked = ch.num_chunks > 1;
    const long O = loop_overhead;
    p.prologue = (long)ch.global.offset_table[0].size() + O + EXP_LUT_ENTRIES + O;
    for (int k = 0; k < ch.num_chunks; k++) {
        const Encoded& e = ch.chunks[k].enc;
        const long slot_beats = e.num_slots / LANES, movable_beats = e.first_fixed_slot / LANES;
        const long macro_entries = (long)e.macro_order.size();
        // Per-cycle trip counts of the owner-side (send, collect) and consumer-side (receive, return)
        // mailbox loops, and the owner side's inner-loop invocations. Meow.
        long owner_trips = (long)ch.chunks[k].shared_local.size(), owner_loops = (long)ch.chunks[k].parcels.size();
        long consumer_trips = (long)ch.chunks[k].external_local.size();
        if (wide) {
            owner_trips = (long)wide->shared_lanes[k].size() / LANES;
            owner_loops = (long)wide->shared_parcels[k].size();
            consumer_trips = (long)wide->external_lanes[k].size() / LANES;
        }
        if (chunked) {
            p.send_reload += slot_beats + O + macro_entries + O;
            p.send        += (long)(owner_trips * (wide ? 1.0 : unburst_cycles)) + owner_loops * O;
            p.receive     += consumer_trips + O;
        }
        p.load  += slot_beats + O + macro_entries + O;
        p.beats += (long)e.issue.size() + GROUP_COUNTS + 2 * O;   // + the dataflow region's fill. Meow.
        if (chunked) p.ret += consumer_trips + O;
        p.drain += movable_beats + O;
        if (chunked) {
            p.collect    += (long)(owner_trips * (wide ? 1.0 : unburst_cycles)) + owner_loops * O;
            p.fold_other += movable_beats + O + macro_entries + O + movable_beats + O;
        } else {
            p.fold_other += macro_entries + O;
        }
    }
    return p;
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
        "usage: chunk_profile [--capacity SLOTS] [--force-k K] [--overhead CYCLES] [--unburst CYCLES] [--small-only]\n"
        "                     (--bookshelf DIR NAME | --def FILE NAME)...\n"
        "  --force-k: one build_chunks at exactly K with capacity unchecked (what-if: a partition that fits K)\n");
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
        const Netlist nl = is_def[d] ? read_def(designs[d].first, designs[d].second)
                                     : read_bookshelf(designs[d].first, designs[d].second);
        const double parse_s = seconds_since(t);

        Chunked ch;
        ch.capacity = force_k ? LONG_MAX : capacity;
        t = std::chrono::steady_clock::now();
        resolve_pin_nodes(nl, ch.global);
        const double resolve_s = seconds_since(t);
        t = std::chrono::steady_clock::now();
        const std::vector<int> order = locality_order(ch.global);
        const double order_s = seconds_since(t);

        // encode_chunked's K loop, timed per attempt. Meow.
        long slot_owners = 0;
        for (NodeKind kind : ch.global.kind) slot_owners += needs_slot(kind);
        std::string attempts;
        double build_s = 0;
        const int first_k = force_k ? force_k : (int)std::max<long>(1, (slot_owners + capacity - 1) / capacity);
        for (int num_chunks = first_k; num_chunks <= (force_k ? force_k : 64); num_chunks++) {
            t = std::chrono::steady_clock::now();
            const bool fits = build_chunks(ch, order, num_chunks, cfg);
            const double s = seconds_since(t);
            build_s += s;
            char buf[64];
            snprintf(buf, sizeof buf, " K=%d:%s %.1fs", num_chunks, fits ? "ok" : "FAIL", s);
            attempts += buf;
            if (fits) { ch.fits = true; break; }
        }
        if (!ch.fits) { printf("%s: no K fits\n", nl.name.c_str()); continue; }

        t = std::chrono::steady_clock::now();
        std::vector<float> zero_pos(ch.global.kind.size(), 0.0f);   // positions do not change the layout cost. Meow.
        for (int axis = 0; axis < 2; axis++) { volatile size_t sink = chunked_device_arrays(ch, axis, zero_pos).records.size(); (void)sink; }
        const double device_arrays_s = seconds_since(t);
        const double startup_s = parse_s + resolve_s + order_s + build_s + device_arrays_s;

        long max_slots = 0, movable = 0;
        for (const Chunk& c : ch.chunks) max_slots = std::max(max_slots, c.enc.num_slots);
        for (char m : nl.movable) movable += m;
        printf("\n%s  movable=%ld K=%d external=%ld (%.1f%%) max_slots=%ld\n", nl.name.c_str(), movable, ch.num_chunks,
               ch.externals, 100.0 * ch.externals / movable, max_slots);
        printf("  host s   total %6.1f | parse %5.1f resolve %5.1f locality_order %5.1f build_chunks %5.1f [%s ] device_arrays %5.1f\n",
               startup_s, parse_s, resolve_s, order_s, build_s, attempts.c_str() + 1, device_arrays_s);

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
