// beat_packer.cpp -- host prototype of the conflict-free pin-beat packing for on-chip scatter-add
// (#41). Reads a real netlist, builds the beat stream hpwl_computer consumes (same-degree nets,
// LANES pins per beat), assigns every movable node a URAM bank.
// Orders the beats such that:
//   (1) no two movable pins in one beat hit the same bank   -> one RMW per bank per cycle
//   (2) no movable node is updated twice within HAZARD beats -> the read-add-write never races
// then re-derives both properties from the output with an independent checker. See README.md
// for the math and the hardware model it assumes. Meow.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

constexpr int LANES           = 16;
constexpr int MIN_NET_DEGREE  = 2;
constexpr int IGNORE_NET_DEGREE = 100;   // XPlace's mask (ignore_net_degree), dropped on the host. Meow.
constexpr int NO_BANK         = -1;

struct Config {
    int      banks       = 16;
    int      hazard      = 4;    // beats between two updates of one node (URAM read + fadd + write). Meow.
    int      window      = 4096; // how many untaken nets the packer / pending beats the scheduler may scan
    int      repair_passes = 50;
    bool     merge_duplicates = false;  // HW pre-adds lanes of one node on one net before the crossbar. Meow.
    unsigned seed        = 1;
    double   budget_mb   = 8.0;
};

struct Netlist {
    std::string name;
    std::vector<char> movable;              // per node
    std::vector<std::vector<int>> nets;     // live nets only, degree 2..IGNORE_NET_DEGREE (one entry per pin = lane)
    std::vector<std::vector<int>> scatter;  // per net: the movable nodes its lanes update in URAM
};

// ---------------------------------------------------------------------------------------------
// Parsers. Bookshelf (ISPD2005, MMS): "terminal"/"terminal_NI" in .nodes = fixed.
// DEF (ISPD2015): COMPONENTS "+ FIXED" = fixed; "( PIN x )" net terminals are IO pins = fixed.

static Netlist read_bookshelf(const std::string& dir, const std::string& name) {
    Netlist nl;
    const std::string suite_dir = dir.substr(0, dir.find_last_of('/'));
    nl.name = suite_dir.substr(suite_dir.find_last_of('/') + 1) + "/" + name;   // mms/ and ispd2005/ share names
    std::unordered_map<std::string, int> node_id;
    std::ifstream nodes(dir + "/" + name + ".nodes");
    if (!nodes) { fprintf(stderr, "cannot open %s.nodes\n", name.c_str()); exit(2); }
    std::string line;
    while (std::getline(nodes, line)) {
        std::istringstream ss(line);
        std::string first; if (!(ss >> first)) continue;
        if (first[0] == '#' || first == "UCLA" || first == "NumNodes" || first == "NumTerminals") continue;
        std::string width, height, kind; ss >> width >> height >> kind;
        node_id.emplace(first, (int)nl.movable.size());
        nl.movable.push_back(kind.rfind("terminal", 0) == 0 ? 0 : 1);
    }
    std::ifstream nets(dir + "/" + name + ".nets");
    if (!nets) { fprintf(stderr, "cannot open %s.nets\n", name.c_str()); exit(2); }
    std::vector<int> cur; int remaining = 0;
    long header_nets = -1, header_pins = -1, parsed_nets = 0, parsed_pins = 0;
    while (std::getline(nets, line)) {
        std::istringstream ss(line);
        std::string first; if (!(ss >> first)) continue;
        std::string colon;
        if (first == "NumNets") ss >> colon >> header_nets;
        else if (first == "NumPins") ss >> colon >> header_pins;
        else if (first == "NetDegree") {
            ss >> colon >> remaining;
            cur.clear();
            parsed_nets++; parsed_pins += remaining;
            if (remaining == 0) continue;
        } else if (remaining > 0) {
            auto it = node_id.find(first);
            if (it == node_id.end()) { fprintf(stderr, "unknown node %s\n", first.c_str()); exit(2); }
            cur.push_back(it->second);
            if (--remaining == 0 && (int)cur.size() >= MIN_NET_DEGREE) nl.nets.push_back(cur);
        }
    }
    if (parsed_nets != header_nets || parsed_pins != header_pins || remaining != 0) {
        fprintf(stderr, "%s: parsed %ld nets / %ld pins, header says %ld / %ld\n",
                name.c_str(), parsed_nets, parsed_pins, header_nets, header_pins);
        exit(2);
    }
    return nl;
}

static Netlist read_def(const std::string& path, const std::string& name) {
    Netlist nl; nl.name = name;
    std::ifstream in(path);
    if (!in) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(2); }
    std::unordered_map<std::string, int> node_id;
    const int io_pin_node = 0;               // every IO pin maps to one shared fixed node
    nl.movable.push_back(0);
    enum { NONE, COMPS, NETS } section = NONE;
    std::string tok, comp_name;
    std::vector<int> cur;
    bool in_stmt = false, comp_fixed = false, collecting = false;
    while (in >> tok) {
        if (tok == "COMPONENTS") { section = COMPS; in >> tok >> tok; continue; }   // count ;
        if (tok == "NETS" && section != NETS) { section = NETS; in >> tok >> tok; continue; }
        if (tok == "END") { std::string what; in >> what; section = NONE; continue; }
        if (section == COMPS) {
            if (tok == "-") { in >> comp_name; in_stmt = true; comp_fixed = false; }
            else if (tok == "FIXED" || tok == "COVER") comp_fixed = true;
            else if (tok == ";" && in_stmt) {
                node_id.emplace(comp_name, (int)nl.movable.size());
                nl.movable.push_back(comp_fixed ? 0 : 1);
                in_stmt = false;
            }
        } else if (section == NETS) {
            if (tok == "-") { in >> tok; cur.clear(); in_stmt = true; collecting = true; }
            else if (tok == "+") collecting = false;    // routing attributes carry ( x y ) points. Meow.
            else if (tok == "(" && collecting) {
                std::string comp, pin; in >> comp >> pin >> tok;   // ( comp pin )
                if (comp == "PIN") cur.push_back(io_pin_node);
                else {
                    auto it = node_id.find(comp);
                    if (it == node_id.end()) { fprintf(stderr, "unknown comp %s\n", comp.c_str()); exit(2); }
                    cur.push_back(it->second);
                }
            } else if (tok == ";" && in_stmt) {
                if ((int)cur.size() >= MIN_NET_DEGREE) nl.nets.push_back(cur);
                in_stmt = false;
            }
        }
    }
    return nl;
}

// ---------------------------------------------------------------------------------------------
// The packing.

struct Beat {
    int degree;
    std::vector<int> nets;
};

struct Packing {
    std::vector<Beat> beats;
    std::vector<int>  bank;            // per node, NO_BANK for fixed / unused
    std::vector<int>  issue;           // beat index per cycle, -1 = bubble
    std::vector<int>  in_scope_nets;   // nets with degree MIN_NET_DEGREE..LANES
    long ideal_beats  = 0;             // sum over degrees of ceil(nets / nets_per_beat)
};

// Step 1: color movable nodes with `banks` colors so that the pins of each net land in distinct
// banks. Welsh-Powell order (most constrained first), least-loaded legal bank to keep URAM depth
// balanced, then min-conflicts repair. A net that still repeats a bank (or a node with two pins on
// one net) costs a stall wherever it is issued. Beat membership is NOT constrained here -- step 2
// chooses it to fit the colors, which is far looser than coloring to fit a fixed packing. Meow.
static void assign_banks(const Netlist& nl, const Config& cfg, Packing& pk) {
    const int num_nodes = (int)nl.movable.size();
    for (int n = 0; n < (int)nl.nets.size(); n++)
        if ((int)nl.nets[n].size() <= LANES) pk.in_scope_nets.push_back(n);
    std::vector<std::vector<int>> node_nets(num_nodes);
    for (int net : pk.in_scope_nets)
        for (int node : nl.scatter[net]) node_nets[node].push_back(net);

    std::vector<int> order;
    std::vector<long> constraint_degree(num_nodes, 0);
    for (int node = 0; node < num_nodes; node++) {
        if (node_nets[node].empty()) continue;
        for (int net : node_nets[node]) constraint_degree[node] += (long)nl.nets[net].size() - 1;
        order.push_back(node);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return constraint_degree[a] > constraint_degree[b]; });

    pk.bank.assign(num_nodes, NO_BANK);
    std::vector<long> bank_load(cfg.banks, 0);
    for (int node : order) {
        uint64_t forbidden = 0;
        for (int net : node_nets[node])
            for (int other : nl.scatter[net])
                if (pk.bank[other] != NO_BANK) forbidden |= 1ull << pk.bank[other];
        int best = -1;
        for (int k = 0; k < cfg.banks; k++)
            if (!(forbidden >> k & 1) && (best < 0 || bank_load[k] < bank_load[best])) best = k;
        if (best < 0) best = (int)(std::min_element(bank_load.begin(), bank_load.end()) - bank_load.begin());
        pk.bank[node] = best;
        bank_load[best]++;
    }

    std::vector<int> collisions(cfg.banks);
    for (int pass = 0; pass < cfg.repair_passes; pass++) {
        long moved = 0;
        for (int node : order) {
            std::fill(collisions.begin(), collisions.end(), 0);
            for (int net : node_nets[node])
                for (int other : nl.scatter[net])
                    if (other != node) collisions[pk.bank[other]]++;
            const int current = pk.bank[node];
            if (collisions[current] == 0) continue;
            int best = current;
            for (int k = 0; k < cfg.banks; k++)
                if (collisions[k] < collisions[best] ||
                    (collisions[k] == collisions[best] && bank_load[k] < bank_load[best])) best = k;
            if (best == current) continue;
            bank_load[current]--; bank_load[best]++;
            pk.bank[node] = best;
            moved++;
        }
        if (moved == 0) break;
    }
}

// Step 2: fill each beat with same-degree nets whose bank masks are pairwise disjoint -- so a beat
// needs one RMW per bank, and (same node => same bank) no node appears in two nets of a beat.
// First-fit over a shuffled pool, looking at most `window` untaken nets ahead; a beat the window
// can't complete ships partly empty (packing loss, the same cost as a stall). Meow.
static void pack_beats(const Netlist& nl, const Config& cfg, Packing& pk, std::mt19937& rng) {
    std::vector<std::vector<int>> by_degree(LANES + 1);
    for (int net : pk.in_scope_nets) by_degree[nl.nets[net].size()].push_back(net);
    for (int degree = MIN_NET_DEGREE; degree <= LANES; degree++) {
        auto& pool = by_degree[degree];
        std::shuffle(pool.begin(), pool.end(), rng);
        const int nets_per_beat = LANES / degree;
        pk.ideal_beats += (pool.size() + nets_per_beat - 1) / nets_per_beat;
        std::vector<uint64_t> net_mask(pool.size(), 0);
        for (size_t i = 0; i < pool.size(); i++)
            for (int node : nl.scatter[pool[i]]) net_mask[i] |= 1ull << pk.bank[node];
        std::vector<char> taken(pool.size(), 0);
        size_t cursor = 0;
        while (true) {
            while (cursor < pool.size() && taken[cursor]) cursor++;
            if (cursor == pool.size()) break;
            Beat beat; beat.degree = degree;
            uint64_t beat_mask = 0;
            int looked = 0;
            for (size_t i = cursor; i < pool.size() && looked < cfg.window
                                    && (int)beat.nets.size() < nets_per_beat; i++) {
                if (taken[i]) continue;
                looked++;
                if (net_mask[i] & beat_mask) continue;
                beat.nets.push_back(pool[i]);
                beat_mask |= net_mask[i];
                taken[i] = 1;
            }
            pk.beats.push_back(std::move(beat));
        }
    }
}

// Cycles one beat occupies: the most-loaded bank serializes, one RMW per bank per cycle.
static int beat_cycles(const Netlist& nl, const Packing& pk, const Beat& beat, int banks) {
    std::vector<int> hits(banks, 0);
    int worst = 1;
    for (int net : beat.nets)
        for (int node : nl.scatter[net]) worst = std::max(worst, ++hits[pk.bank[node]]);
    return worst;
}

// Step 3: order beats (degree groups stay contiguous and ascending -- resolve_beat's contract) so a
// node is not re-updated within `hazard` cycles; emit a bubble when nothing in the window is ready.
static void schedule_beats(const Netlist& nl, const Config& cfg, Packing& pk) {
    std::vector<long> last_update(nl.movable.size(), -(long)cfg.hazard);
    long cycle = 0;
    size_t group_begin = 0;
    while (group_begin < pk.beats.size()) {
        size_t group_end = group_begin;
        while (group_end < pk.beats.size() && pk.beats[group_end].degree == pk.beats[group_begin].degree) group_end++;
        std::deque<int> pending;
        for (size_t b = group_begin; b < group_end; b++) pending.push_back((int)b);
        while (!pending.empty()) {
            int pick = -1;
            for (int scan = 0; scan < (int)pending.size() && scan < cfg.window && pick < 0; scan++) {
                bool ready = true;
                for (int net : pk.beats[pending[scan]].nets)
                    for (int node : nl.scatter[net])
                        if (cycle - last_update[node] < cfg.hazard) ready = false;
                if (ready) pick = scan;
            }
            if (pick < 0) { pk.issue.push_back(-1); cycle++; continue; }
            const int b = pending[pick];
            pending.erase(pending.begin() + pick);
            const int occupancy = beat_cycles(nl, pk, pk.beats[b], cfg.banks);
            for (int net : pk.beats[b].nets)
                for (int node : nl.scatter[net]) last_update[node] = cycle + occupancy - 1;
            pk.issue.push_back(b);
            for (int extra = 1; extra < occupancy; extra++) pk.issue.push_back(-2);   // bank-conflict stall
            cycle += occupancy;
        }
        group_begin = group_end;
    }
}

// ---------------------------------------------------------------------------------------------
// Independent checker: re-derives every invariant from the emitted issue sequence and bank map.
// Structural violations are failures; bank-conflict stalls are allowed but must be accounted
// for exactly (each stall cycle is a -2 entry right after its beat). Meow.

static int check(const Netlist& nl, const Config& cfg, const Packing& pk) {
    int failures = 0;
    auto fail = [&](const char* what, long where) {
        if (failures++ < 10) fprintf(stderr, "  CHECK FAIL: %s (at %ld)\n", what, where);
    };
    std::vector<int> net_seen(nl.nets.size(), 0);
    std::vector<long> last_update(nl.movable.size(), -(long)cfg.hazard);
    int prev_degree = 0;
    for (long cycle = 0; cycle < (long)pk.issue.size(); cycle++) {
        const int b = pk.issue[cycle];
        if (b < 0) continue;
        const Beat& beat = pk.beats[b];
        if (beat.degree < prev_degree) fail("degree groups not ascending", cycle);
        prev_degree = beat.degree;
        if ((int)beat.nets.size() > LANES / beat.degree) fail("beat over capacity", cycle);
        int stall = 0;
        while (cycle + 1 + stall < (long)pk.issue.size() && pk.issue[cycle + 1 + stall] == -2) stall++;
        // A node with two pins on ONE net is inherent to the netlist (costs a stall, allowed);
        // the same node in two DIFFERENT nets of a beat is a packer bug. Meow.
        std::vector<int> hits(cfg.banks, 0), seen_nodes;
        int worst = 1;
        for (int net : beat.nets) {
            if ((int)nl.nets[net].size() != beat.degree) fail("mixed degree in beat", cycle);
            net_seen[net]++;
            const size_t earlier_nets_end = seen_nodes.size();
            for (size_t pin = 0; pin < nl.nets[net].size(); pin++) {
                const int node = nl.nets[net][pin];
                if (!nl.movable[node]) continue;
                const auto pin_begin = nl.nets[net].begin();
                if (cfg.merge_duplicates && std::find(pin_begin, pin_begin + pin, node) != pin_begin + pin) continue;
                const int bank = pk.bank[node];
                if (bank < 0 || bank >= cfg.banks) { fail("movable node without bank", node); continue; }
                if (std::count(seen_nodes.begin(), seen_nodes.begin() + earlier_nets_end, node))
                    fail("node in two nets of one beat", cycle);
                seen_nodes.push_back(node);
                worst = std::max(worst, ++hits[bank]);
                if (cycle - last_update[node] < cfg.hazard) fail("RAW hazard", cycle);
            }
        }
        if (stall != worst - 1) fail("bank-conflict stalls not accounted", cycle);
        for (int node : seen_nodes) last_update[node] = cycle + stall;
    }
    for (int net : pk.in_scope_nets)
        if (net_seen[net] != 1) fail("in-scope net not issued exactly once", net);
    return failures;
}

// ---------------------------------------------------------------------------------------------

static void report(const Netlist& nl, const Config& cfg, const Packing& pk, double seconds, int failures) {
    long movable = 0, pins_in_scope = 0, pins_17_100 = 0, pins_all = 0;
    for (char m : nl.movable) movable += m;
    for (const auto& net : nl.nets) {
        pins_all += net.size();
        if ((int)net.size() <= LANES) pins_in_scope += net.size(); else pins_17_100 += net.size();
    }
    long bubbles = 0, stalls = 0, issued = 0;
    for (int b : pk.issue) { if (b == -1) bubbles++; else if (b == -2) stalls++; else issued++; }
    std::vector<long> bank_load(cfg.banks, 0);
    for (int bank : pk.bank) if (bank != NO_BANK) bank_load[bank]++;
    const long max_depth = *std::max_element(bank_load.begin(), bank_load.end());
    const long banked_nodes = std::accumulate(bank_load.begin(), bank_load.end(), 0L);
    // two float arrays (position + gradient accumulator), one axis at a time, depth = deepest bank
    const double uram_mb = 2.0 * 4.0 * max_depth * cfg.banks / 1e6;
    const double efficiency = (double)pk.ideal_beats / pk.issue.size();
    printf("%-22s %8ld %9ld %6.2f%% %8ld %8zu %6.2f%% %7ld %7ld %7.2f%% %6.3f %7.2f%s %6.1fs %s\n",
           nl.name.c_str(), movable, pins_in_scope, 100.0 * pins_17_100 / pins_all,
           pk.ideal_beats, pk.beats.size(), 100.0 * (pk.beats.size() - pk.ideal_beats) / pk.ideal_beats,
           stalls, bubbles, 100.0 * efficiency,
           banked_nodes ? (double)max_depth * cfg.banks / banked_nodes : 0.0,
           uram_mb, uram_mb > cfg.budget_mb ? "!" : " ", seconds, failures ? "FAIL" : "ok");
    (void)issued;
}

static void usage() {
    fprintf(stderr,
        "usage: beat_packer [--banks B] [--hazard H] [--window W] [--seed S] [--budget-mb M] [--merge-dups] [--repair N]\n"
        "                   (--bookshelf DIR NAME | --def FILE NAME)...\n");
    exit(2);
}

int main(int argc, char** argv) {
    Config cfg;
    std::vector<std::pair<std::string, std::string>> designs;   // (path spec, name)
    std::vector<char> is_def;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() { if (i + 1 >= argc) usage(); return std::string(argv[++i]); };
        if      (arg == "--banks")     cfg.banks     = std::stoi(next());
        else if (arg == "--hazard")    cfg.hazard    = std::stoi(next());
        else if (arg == "--window")    cfg.window    = std::stoi(next());
        else if (arg == "--seed")      cfg.seed      = (unsigned)std::stoul(next());
        else if (arg == "--merge-dups") cfg.merge_duplicates = true;
        else if (arg == "--repair")    cfg.repair_passes = std::stoi(next());
        else if (arg == "--budget-mb") cfg.budget_mb = std::stod(next());
        else if (arg == "--bookshelf" || arg == "--def") {
            std::string path = next(), name = next();
            designs.emplace_back(path, name); is_def.push_back(arg == "--def");
        } else usage();
    }
    if (designs.empty() || cfg.banks < 1 || cfg.banks > 64) usage();

    printf("banks=%d hazard=%d window=%d seed=%u merge_dups=%d  (URAM = 2 float arrays x deepest bank x banks, one axis)\n",
           cfg.banks, cfg.hazard, cfg.window, cfg.seed, (int)cfg.merge_duplicates);
    printf("%-22s %8s %9s %7s %8s %8s %7s %7s %7s %8s %6s %8s %7s\n", "design", "movable", "pins<=16",
           "pin>16", "ideal_bt", "packed", "packloss", "stalls", "bubbles", "effic", "imbal", "URAM_MB", "time");
    int total_failures = 0;
    for (size_t d = 0; d < designs.size(); d++) {
        const auto start = std::chrono::steady_clock::now();
        Netlist nl = is_def[d] ? read_def(designs[d].first, designs[d].second)
                               : read_bookshelf(designs[d].first, designs[d].second);
        nl.nets.erase(std::remove_if(nl.nets.begin(), nl.nets.end(),
                                     [](const std::vector<int>& net) { return (int)net.size() > IGNORE_NET_DEGREE; }),
                      nl.nets.end());
        for (const auto& net : nl.nets) {
            std::vector<int> nodes;
            for (int node : net)
                if (nl.movable[node] && !(cfg.merge_duplicates && std::count(nodes.begin(), nodes.end(), node)))
                    nodes.push_back(node);
            nl.scatter.push_back(std::move(nodes));
        }
        std::mt19937 rng(cfg.seed);
        Packing pk;
        assign_banks(nl, cfg, pk);
        pack_beats(nl, cfg, pk, rng);
        schedule_beats(nl, cfg, pk);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const int failures = check(nl, cfg, pk);
        total_failures += failures;
        report(nl, cfg, pk, seconds, failures);
        fflush(stdout);
    }
    return total_failures ? 1 : 0;
}
