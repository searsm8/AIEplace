// beat_packer.cpp -- host prototype of the pin-record stream for on-chip gather + scatter-add (#41).
//
// Reads a real netlist and encodes it as the static per-axis record stream the gradient datapath
// consumes: one 32-bit record per lane, 16 lanes per 512-bit beat, same-degree nets per beat.
//
//     record = node_slot << offset_bits | offset_idx
//     node_slot  = row * banks + bank   -> bank = node_slot % banks, URAM row = node_slot / banks
//     offset_idx = index into this axis's on-chip table of distinct pin offsets
//
// The datapath reads pos[node_slot] + offset_table[offset_idx] (gather), runs the trees, and does
// grad[node_slot] += g (scatter-add), both against banked URAM. The host guarantees, per beat:
//   (1) distinct node_slots hit distinct banks        -> one access per bank per cycle
//   (2) a movable node_slot is not updated within `hazard` beats of its last update
//   (3) a node's repeated pins on one net sit in adjacent lanes (same node_slot) -> the hardware
//       shares one read and pre-adds the lanes before the crossbar
// The encoded stream is then decoded back to a netlist and compared with the input by an
// independent checker. See README.md. Meow.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

constexpr int LANES             = 16;
constexpr int MIN_NET_DEGREE    = 2;
constexpr int IGNORE_NET_DEGREE = 100;   // XPlace's mask (ignore_net_degree), dropped on the host. Meow.
constexpr int NO_BANK           = -1;
constexpr int BUBBLE            = -1;    // an issued all-EMPTY beat (hazard wait)
constexpr int URAM_WORDS        = 4096;  // URAM288: 4K x 72 bits, fixed shape
constexpr int FLOATS_PER_WORD   = 2;     // 64 of the 72 bits used
constexpr int VC1902_URAMS      = 463;

struct Config {
    int      banks         = 32;
    int      hazard        = 4;    // beats between two updates of one node (URAM read + fadd + write). Meow.
    int      window        = 4096; // untaken nets the packer / pending beats the scheduler may scan
    int      repair_passes = 50;
    unsigned seed          = 1;
};

struct Pin {
    int node;
    int offset_key[2];   // per axis, index into Netlist::offset_value
};

struct Netlist {
    std::string name;
    std::vector<char> movable;                  // per node
    std::vector<std::vector<Pin>> nets;         // live nets, degree 2..IGNORE_NET_DEGREE
    std::vector<float> offset_value[2];         // per axis, distinct offset values
    std::vector<std::vector<int>> unique_nodes; // per net: distinct nodes, the ones its lanes touch in URAM
    std::vector<int> base_node;                  // per node: itself, or the fixed node a pseudo-node stands for
    std::vector<std::array<float, 2>> pinned_offset;  // per node: 0, or the offset baked into a pseudo-node
};

using GeomPin = std::tuple<int, float, float>;   // (node as parsed, total offset x, total offset y)

// The in-scope nets as geometry: what the device must reconstruct, independent of any encoding. Meow.
static std::vector<std::vector<GeomPin>> in_scope_geometry(const Netlist& nl) {
    std::vector<std::vector<GeomPin>> nets;
    for (const auto& net : nl.nets) {
        if ((int)net.size() > LANES) continue;
        std::vector<GeomPin> geom;
        for (const Pin& pin : net)
            geom.emplace_back(pin.node, nl.offset_value[0][pin.offset_key[0]], nl.offset_value[1][pin.offset_key[1]]);
        std::sort(geom.begin(), geom.end());
        nets.push_back(std::move(geom));
    }
    std::sort(nets.begin(), nets.end());
    return nets;
}

// Offset dictionary: one entry per distinct offset value. Meow.
struct OffsetDict {
    std::unordered_map<std::string, int> index;
    int key(const std::string& k, float value, std::vector<float>& values) {
        auto it = index.find(k);
        if (it != index.end()) return it->second;
        index.emplace(k, (int)values.size());
        values.push_back(value);
        return (int)values.size() - 1;
    }
};

static int offset_key_by_value(OffsetDict& dict, float value, std::vector<float>& values) {
    uint32_t value_bits; std::memcpy(&value_bits, &value, sizeof value_bits);   // key on value, not spelling
    return dict.key(std::to_string(value_bits), value, values);
}

// ---------------------------------------------------------------------------------------------
// Parsers. Bookshelf (ISPD2005, MMS): "terminal"/"terminal_NI" in .nodes = fixed, pin offsets
// from .nets. DEF (ISPD2015): COMPONENTS "+ FIXED" = fixed; each "( PIN x )" is its own fixed IO
// node at offset 0. DEF pin offsets come from cells.lef next to the DEF, by the same rule as the
// sw_only parser (DataBase::lef_pin_cbk): center of the first RECT of the first PORT.

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
    OffsetDict dict[2];
    std::vector<Pin> cur; int remaining = 0;
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
        } else if (remaining > 0) {
            auto it = node_id.find(first);
            if (it == node_id.end()) { fprintf(stderr, "unknown node %s\n", first.c_str()); exit(2); }
            std::string direction, offset_text[2] = {"0", "0"};
            ss >> direction >> colon >> offset_text[0] >> offset_text[1];
            Pin pin; pin.node = it->second;
            for (int axis = 0; axis < 2; axis++)
                pin.offset_key[axis] = offset_key_by_value(dict[axis], std::stof(offset_text[axis]), nl.offset_value[axis]);
            cur.push_back(pin);
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

static std::unordered_map<std::string, std::array<float, 2>> read_lef_pin_offsets(const std::string& path) {
    std::unordered_map<std::string, std::array<float, 2>> offsets;   // "master/pin" -> (x, y)
    std::ifstream in(path);
    if (!in) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(2); }
    std::string line, macro, pin;
    bool want_rect = false;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string tok; if (!(ss >> tok)) continue;
        if (tok == "MACRO") ss >> macro;
        else if (tok == "PIN") { ss >> pin; want_rect = false; }
        else if (tok == "PORT" && !pin.empty() && !offsets.count(macro + "/" + pin)) want_rect = true;
        else if (tok == "RECT" && want_rect) {
            float x0, y0, x1, y1; ss >> x0 >> y0 >> x1 >> y1;
            offsets[macro + "/" + pin] = {(x0 + x1) / 2.0f, (y0 + y1) / 2.0f};
            want_rect = false;
        } else if (tok == "END") {
            std::string what; ss >> what;
            if (what == pin) { pin.clear(); want_rect = false; }
        }
    }
    return offsets;
}

static Netlist read_def(const std::string& path, const std::string& name) {
    Netlist nl; nl.name = name;
    const auto lef_offsets = read_lef_pin_offsets(path.substr(0, path.find_last_of('/')) + "/cells.lef");
    std::ifstream in(path);
    if (!in) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(2); }
    std::unordered_map<std::string, int> node_id, io_pin_node;
    std::unordered_map<std::string, std::string> master_of;
    OffsetDict dict[2];
    enum { NONE, COMPS, NETS } section = NONE;
    std::string tok, comp_name, comp_master;
    std::vector<Pin> cur;
    bool in_stmt = false, comp_fixed = false, collecting = false;
    while (in >> tok) {
        if (tok == "COMPONENTS") { section = COMPS; in >> tok >> tok; continue; }   // count ;
        if (tok == "NETS" && section != NETS) { section = NETS; in >> tok >> tok; continue; }
        if (tok == "END") { std::string what; in >> what; section = NONE; continue; }
        if (section == COMPS) {
            if (tok == "-") { in >> comp_name >> comp_master; in_stmt = true; comp_fixed = false; }
            else if (tok == "FIXED" || tok == "COVER") comp_fixed = true;
            else if (tok == ";" && in_stmt) {
                node_id.emplace(comp_name, (int)nl.movable.size());
                master_of.emplace(comp_name, comp_master);
                nl.movable.push_back(comp_fixed ? 0 : 1);
                in_stmt = false;
            }
        } else if (section == NETS) {
            if (tok == "-") { in >> tok; cur.clear(); in_stmt = true; collecting = true; }
            else if (tok == "+") collecting = false;    // routing attributes carry ( x y ) points. Meow.
            else if (tok == "(" && collecting) {
                std::string comp, pin_name; in >> comp >> pin_name >> tok;   // ( comp pin )
                Pin pin;
                std::array<float, 2> offset = {0.0f, 0.0f};
                if (comp == "PIN") {
                    auto it = io_pin_node.find(pin_name);
                    if (it == io_pin_node.end()) {
                        it = io_pin_node.emplace(pin_name, (int)nl.movable.size()).first;
                        nl.movable.push_back(0);
                    }
                    pin.node = it->second;
                } else {
                    auto it = node_id.find(comp);
                    if (it == node_id.end()) { fprintf(stderr, "unknown comp %s\n", comp.c_str()); exit(2); }
                    pin.node = it->second;
                    auto lef = lef_offsets.find(master_of[comp] + "/" + pin_name);
                    if (lef == lef_offsets.end()) { fprintf(stderr, "no LEF pin %s/%s\n", master_of[comp].c_str(), pin_name.c_str()); exit(2); }
                    offset = lef->second;
                }
                for (int axis = 0; axis < 2; axis++)
                    pin.offset_key[axis] = offset_key_by_value(dict[axis], offset[axis], nl.offset_value[axis]);
                cur.push_back(pin);
            } else if (tok == ";" && in_stmt) {
                if ((int)cur.size() >= MIN_NET_DEGREE) nl.nets.push_back(cur);
                in_stmt = false;
            }
        }
    }
    return nl;
}

// A fixed pin's absolute position never changes, so it can be its own fixed pseudo-node whose
// position slot already holds node position + offset. Its record then carries offset_idx of the
// zero offset, and the offset tables keep only movable pins' offsets -- bounded by the cell
// library, not by macro pin count. One pseudo-node per distinct (fixed node, offset x, offset y). Meow.
static void resolve_fixed_pins(Netlist& nl) {
    nl.base_node.resize(nl.movable.size());
    std::iota(nl.base_node.begin(), nl.base_node.end(), 0);
    nl.pinned_offset.assign(nl.movable.size(), {0.0f, 0.0f});
    std::map<std::array<int, 3>, int> pseudo_node;
    std::vector<char> used[2];
    for (int axis = 0; axis < 2; axis++) used[axis].assign(nl.offset_value[axis].size(), 0);
    for (auto& net : nl.nets)
        for (Pin& pin : net) {
            if (nl.movable[pin.node]) { for (int axis = 0; axis < 2; axis++) used[axis][pin.offset_key[axis]] = 1; continue; }
            auto it = pseudo_node.emplace(std::array<int, 3>{pin.node, pin.offset_key[0], pin.offset_key[1]},
                                          (int)nl.movable.size()).first;
            if (it->second == (int)nl.movable.size()) {
                nl.movable.push_back(0);
                nl.base_node.push_back(pin.node);
                nl.pinned_offset.push_back({nl.offset_value[0][pin.offset_key[0]], nl.offset_value[1][pin.offset_key[1]]});
            }
            pin.node = it->second;
            pin.offset_key[0] = pin.offset_key[1] = -1;   // zero offset, remapped below
        }
    for (int axis = 0; axis < 2; axis++) {
        std::vector<int> remap(nl.offset_value[axis].size(), -1);
        std::vector<float> kept = {0.0f};                 // index 0 = the zero offset of every fixed pin
        for (size_t key = 0; key < remap.size(); key++)
            if (used[axis][key]) { remap[key] = (int)kept.size(); kept.push_back(nl.offset_value[axis][key]); }
        for (auto& net : nl.nets)
            for (Pin& pin : net) pin.offset_key[axis] = pin.offset_key[axis] < 0 ? 0 : remap[pin.offset_key[axis]];
        nl.offset_value[axis] = std::move(kept);
    }
}

// ---------------------------------------------------------------------------------------------
// Packing: color nodes into banks -> pack beats -> schedule -> number slots -> encode.

struct Beat {
    int degree;
    std::vector<int> nets;
};

struct Packing {
    std::vector<int>  in_scope_nets;       // degree MIN_NET_DEGREE..LANES
    std::vector<int>  bank;                // per node, NO_BANK if it touches no in-scope net
    long              colliding_nets = 0;  // nets whose own nodes share a bank: not encodable
    std::vector<Beat> beats;
    long              ideal_beats = 0;     // sum over degrees of ceil(nets / nets_per_beat)
    std::vector<int>  issue;               // beat index per stream position, BUBBLE = all-EMPTY beat
    std::vector<int>  issue_degree;        // degree group of each stream position (bubbles included)
    std::vector<long> node_slot;           // per node, -1 if none
    long              first_fixed_slot = 0;
    long              max_slot = 0;
    int               node_bits = 0, offset_bits = 0;
    long              uram_grad = 0, uram_pos = 0;
    std::vector<uint32_t> records[2];      // per axis, LANES per stream position
    std::vector<int>  beat_count;          // cumulative stream positions per degree (resolve_beat)
};

// Step 1: color every node on an in-scope net (movable and fixed: fixed ones are read for their
// position) so that each net's distinct nodes land in distinct banks. Welsh-Powell order, least-
// loaded legal bank within the node's class (movable / fixed, balanced separately because they
// occupy separate row ranges), then min-conflicts repair. Meow.
static void assign_banks(const Netlist& nl, const Config& cfg, Packing& pk) {
    const int num_nodes = (int)nl.movable.size();
    std::vector<std::vector<int>> node_nets(num_nodes);
    for (int net : pk.in_scope_nets)
        for (int node : nl.unique_nodes[net]) node_nets[node].push_back(net);

    std::vector<int> order;
    std::vector<long> constraint_degree(num_nodes, 0);
    for (int node = 0; node < num_nodes; node++) {
        if (node_nets[node].empty()) continue;
        for (int net : node_nets[node]) constraint_degree[node] += (long)nl.unique_nodes[net].size() - 1;
        order.push_back(node);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return constraint_degree[a] > constraint_degree[b]; });

    pk.bank.assign(num_nodes, NO_BANK);
    std::vector<long> bank_load[2] = {std::vector<long>(cfg.banks, 0), std::vector<long>(cfg.banks, 0)};
    for (int node : order) {
        auto& load = bank_load[(int)nl.movable[node]];
        uint64_t forbidden = 0;
        for (int net : node_nets[node])
            for (int other : nl.unique_nodes[net])
                if (pk.bank[other] != NO_BANK) forbidden |= 1ull << pk.bank[other];
        int best = -1;
        for (int k = 0; k < cfg.banks; k++)
            if (!(forbidden >> k & 1) && (best < 0 || load[k] < load[best])) best = k;
        if (best < 0) best = (int)(std::min_element(load.begin(), load.end()) - load.begin());
        pk.bank[node] = best;
        load[best]++;
    }

    std::vector<int> collisions(cfg.banks);
    for (int pass = 0; pass < cfg.repair_passes; pass++) {
        long moved = 0;
        for (int node : order) {
            auto& load = bank_load[(int)nl.movable[node]];
            std::fill(collisions.begin(), collisions.end(), 0);
            for (int net : node_nets[node])
                for (int other : nl.unique_nodes[net])
                    if (other != node) collisions[pk.bank[other]]++;
            const int current = pk.bank[node];
            if (collisions[current] == 0) continue;
            int best = current;
            for (int k = 0; k < cfg.banks; k++)
                if (collisions[k] < collisions[best] ||
                    (collisions[k] == collisions[best] && load[k] < load[best])) best = k;
            if (best == current) continue;
            load[current]--; load[best]++;
            pk.bank[node] = best;
            moved++;
        }
        if (moved == 0) break;
    }

    for (int net : pk.in_scope_nets) {
        uint64_t mask = 0;
        for (int node : nl.unique_nodes[net]) {
            if (mask >> pk.bank[node] & 1) { pk.colliding_nets++; break; }
            mask |= 1ull << pk.bank[node];
        }
    }
}

// Step 2: fill each beat with same-degree nets whose bank masks are pairwise disjoint. Same node
// => same bank, so this also keeps a node out of two nets of one beat. First-fit over a shuffled
// pool, at most `window` untaken nets ahead; a beat the window can't complete ships partly EMPTY. Meow.
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
            for (int node : nl.unique_nodes[pool[i]]) net_mask[i] |= 1ull << pk.bank[node];
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

// Step 3: order beats (degree groups contiguous and ascending -- resolve_beat's contract) so no
// movable node is re-updated within `hazard` positions; emit a BUBBLE when nothing in the window
// is ready. Fixed nodes are only read, so they carry no hazard. Meow.
static void schedule_beats(const Netlist& nl, const Config& cfg, Packing& pk) {
    std::vector<long> last_update(nl.movable.size(), -(long)cfg.hazard);
    long position = 0;
    size_t group_begin = 0;
    while (group_begin < pk.beats.size()) {
        const int degree = pk.beats[group_begin].degree;
        size_t group_end = group_begin;
        while (group_end < pk.beats.size() && pk.beats[group_end].degree == degree) group_end++;
        std::deque<int> pending;
        for (size_t b = group_begin; b < group_end; b++) pending.push_back((int)b);
        while (!pending.empty()) {
            int pick = -1;
            for (int scan = 0; scan < (int)pending.size() && scan < cfg.window && pick < 0; scan++) {
                bool ready = true;
                for (int net : pk.beats[pending[scan]].nets)
                    for (int node : nl.unique_nodes[net])
                        if (nl.movable[node] && position - last_update[node] < cfg.hazard) ready = false;
                if (ready) pick = scan;
            }
            pk.issue_degree.push_back(degree);
            if (pick < 0) { pk.issue.push_back(BUBBLE); position++; continue; }
            const int b = pending[pick];
            pending.erase(pending.begin() + pick);
            for (int net : pk.beats[b].nets)
                for (int node : nl.unique_nodes[net]) if (nl.movable[node]) last_update[node] = position;
            pk.issue.push_back(b);
            position++;
        }
        group_begin = group_end;
    }
}

static int bits_for(long max_value) {   // bits to represent 0..max_value
    int bits = 0;
    while ((1L << bits) <= max_value) bits++;
    return bits;
}

// Step 4: node_slot = row * banks + bank. Movable rows first in every bank, fixed rows after, so
// `node_slot >= first_fixed_slot` is the hardware's "read position, skip gradient write" test.
// The all-ones node_slot is reserved as EMPTY, so node_bits leaves room above max_slot. Meow.
static void assign_node_slots(const Netlist& nl, const Config& cfg, Packing& pk) {
    std::vector<long> rows[2] = {std::vector<long>(cfg.banks, 0), std::vector<long>(cfg.banks, 0)};
    for (int node = 0; node < (int)nl.movable.size(); node++)
        if (pk.bank[node] != NO_BANK) rows[(int)nl.movable[node]][pk.bank[node]]++;
    const long movable_rows = *std::max_element(rows[1].begin(), rows[1].end());
    const long fixed_rows   = *std::max_element(rows[0].begin(), rows[0].end());
    pk.first_fixed_slot = movable_rows * cfg.banks;

    std::vector<long> next_row[2] = {std::vector<long>(cfg.banks, movable_rows), std::vector<long>(cfg.banks, 0)};
    pk.node_slot.assign(nl.movable.size(), -1);
    for (int node = 0; node < (int)nl.movable.size(); node++) {
        const int bank = pk.bank[node];
        if (bank == NO_BANK) continue;
        pk.node_slot[node] = next_row[(int)nl.movable[node]][bank]++ * cfg.banks + bank;
        pk.max_slot = std::max(pk.max_slot, pk.node_slot[node]);
    }
    pk.node_bits = std::max(bits_for(pk.max_slot + 1), bits_for(cfg.banks - 1));   // +1: EMPTY above every real slot
    const long words_per_uram = (long)URAM_WORDS * FLOATS_PER_WORD;
    pk.uram_grad = cfg.banks * ((movable_rows + words_per_uram - 1) / words_per_uram);
    pk.uram_pos  = cfg.banks * ((movable_rows + fixed_rows + words_per_uram - 1) / words_per_uram);
}

// Step 5: emit the per-axis record streams. Net k of a beat occupies lanes [k*d, (k+1)*d); its pins
// are sorted by node_slot so a node's repeated pins are adjacent; every other lane is EMPTY. Meow.
static void encode(const Netlist& nl, const Config& cfg, Packing& pk) {
    long max_offset_idx = 0;
    for (int axis = 0; axis < 2; axis++) max_offset_idx = std::max(max_offset_idx, (long)nl.offset_value[axis].size() - 1);
    pk.offset_bits = bits_for(max_offset_idx);
    const uint32_t empty_slot = (uint32_t)((1ull << pk.node_bits) - 1);
    for (int axis = 0; axis < 2; axis++) {
        pk.records[axis].assign(pk.issue.size() * LANES, empty_slot << pk.offset_bits);
    }
    for (size_t position = 0; position < pk.issue.size(); position++) {
        if (pk.issue[position] == BUBBLE) continue;
        const Beat& beat = pk.beats[pk.issue[position]];
        for (size_t k = 0; k < beat.nets.size(); k++) {
            std::vector<Pin> pins = nl.nets[beat.nets[k]];
            std::stable_sort(pins.begin(), pins.end(),
                             [&](const Pin& a, const Pin& b) { return pk.node_slot[a.node] < pk.node_slot[b.node]; });
            for (int p = 0; p < beat.degree; p++)
                for (int axis = 0; axis < 2; axis++)
                    pk.records[axis][position * LANES + k * beat.degree + p] =
                        (uint32_t)pk.node_slot[pins[p].node] << pk.offset_bits | (uint32_t)pins[p].offset_key[axis];
        }
    }
    pk.beat_count.assign(LANES - MIN_NET_DEGREE + 1, 0);
    for (int degree : pk.issue_degree)
        for (int i = degree - MIN_NET_DEGREE; i < (int)pk.beat_count.size(); i++) pk.beat_count[i]++;
    (void)cfg;
}

// ---------------------------------------------------------------------------------------------
// Independent checker: decodes the record streams using only what the device would see (records,
// beat_count, node_bits/offset_bits, banks, first_fixed_slot, offset tables) plus the slot->node
// inverse, and requires the decoded geometry -- (parsed node, total offset) per pin -- to equal
// `expected`, taken from the netlist BEFORE the fixed-pin rewrite, so a wrong rewrite is caught
// too. Every beat must also satisfy the bank, hazard and adjacency rules. Meow.

static int check(const Netlist& nl, const Config& cfg, const Packing& pk,
                 const std::vector<std::vector<GeomPin>>& expected) {
    int failures = 0;
    auto fail = [&](const char* what, long where) {
        if (failures++ < 10) fprintf(stderr, "  CHECK FAIL: %s (at %ld)\n", what, where);
    };
    if (pk.node_bits + pk.offset_bits > 32) { fail("record does not fit 32 bits", pk.node_bits + pk.offset_bits); return failures; }
    const uint32_t empty_slot  = (uint32_t)((1ull << pk.node_bits) - 1);
    const uint32_t offset_mask = (uint32_t)((1ull << pk.offset_bits) - 1);
    std::unordered_map<uint32_t, int> slot_to_node;
    for (int node = 0; node < (int)nl.movable.size(); node++)
        if (pk.node_slot[node] >= 0) slot_to_node.emplace((uint32_t)pk.node_slot[node], node);

    std::vector<std::vector<GeomPin>> decoded;
    std::unordered_map<uint32_t, long> last_update;
    const long num_positions = (long)pk.records[0].size() / LANES;
    for (long position = 0; position < num_positions; position++) {
        int degree = MIN_NET_DEGREE;   // resolve_beat's rule, from cumulative counts
        for (int count : pk.beat_count) if (position >= count) degree++;
        const int nets_per_beat = LANES / degree;
        std::vector<uint32_t> beat_slots;
        for (int lane = 0; lane < LANES; lane++) {
            const uint32_t rx = pk.records[0][position * LANES + lane], ry = pk.records[1][position * LANES + lane];
            if (rx >> pk.offset_bits != ry >> pk.offset_bits) fail("x and y node_slot differ", position);
            if (lane >= nets_per_beat * degree && rx >> pk.offset_bits != empty_slot) fail("tail lane not EMPTY", position);
        }
        for (int k = 0; k < nets_per_beat; k++) {
            const long base = position * LANES + (long)k * degree;
            int empty_lanes = 0;
            for (int p = 0; p < degree; p++) empty_lanes += (pk.records[0][base + p] >> pk.offset_bits) == empty_slot;
            if (empty_lanes == degree) continue;
            if (empty_lanes) { fail("net partly EMPTY", position); continue; }
            std::vector<GeomPin> net;
            std::vector<uint32_t> net_slots;
            for (int p = 0; p < degree; p++) {
                const uint32_t slot = pk.records[0][base + p] >> pk.offset_bits;
                auto it = slot_to_node.find(slot);
                if (it == slot_to_node.end()) { fail("node_slot maps to no node", position); continue; }
                if (!net_slots.empty() && net_slots.back() != slot &&
                    std::count(net_slots.begin(), net_slots.end(), slot)) fail("repeated node not adjacent", position);
                if (net_slots.empty() || net_slots.back() != slot) net_slots.push_back(slot);
                const int node = it->second;
                const uint32_t offset_idx_x = pk.records[0][base + p] & offset_mask;
                const uint32_t offset_idx_y = pk.records[1][base + p] & offset_mask;
                if (offset_idx_x >= nl.offset_value[0].size() || offset_idx_y >= nl.offset_value[1].size()) {
                    fail("offset_idx past table end", position); continue;
                }
                net.emplace_back(nl.base_node[node], nl.pinned_offset[node][0] + nl.offset_value[0][offset_idx_x],
                                                     nl.pinned_offset[node][1] + nl.offset_value[1][offset_idx_y]);
            }
            for (uint32_t slot : net_slots) {
                if (std::count(beat_slots.begin(), beat_slots.end(), slot)) fail("node in two nets of one beat", position);
                beat_slots.push_back(slot);
            }
            std::sort(net.begin(), net.end());
            decoded.push_back(std::move(net));
        }
        uint64_t banks_used = 0;
        for (uint32_t slot : beat_slots) {
            const int bank = (int)(slot % cfg.banks);
            if (banks_used >> bank & 1) fail("bank hit twice in one beat", position);
            banks_used |= 1ull << bank;
            if (slot < pk.first_fixed_slot) {
                auto it = last_update.find(slot);
                if (it != last_update.end() && position - it->second < cfg.hazard) fail("RAW hazard", position);
                last_update[slot] = position;
            }
        }
    }
    for (const auto& entry : slot_to_node)
        if ((entry.first < pk.first_fixed_slot) != (bool)nl.movable[entry.second]) fail("movable/fixed slot range wrong", entry.first);

    std::sort(decoded.begin(), decoded.end());
    if (decoded != expected) fail("decoded netlist differs from input", (long)decoded.size() - (long)expected.size());
    return failures;
}

// ---------------------------------------------------------------------------------------------

static void report(const Netlist& nl, const Packing& pk, double seconds, int failures) {
    long movable = 0, fixed_slots = 0, pins_in_scope = 0, pins_17_100 = 0, pins_all = 0, bubbles = 0;
    for (char m : nl.movable) movable += m;
    for (int node = 0; node < (int)nl.movable.size(); node++) fixed_slots += (!nl.movable[node] && pk.node_slot[node] >= 0);
    for (const auto& net : nl.nets) {
        pins_all += net.size();
        if ((int)net.size() <= LANES) pins_in_scope += net.size(); else pins_17_100 += net.size();
    }
    for (int b : pk.issue) bubbles += (b == BUBBLE);
    const double efficiency = (double)pk.ideal_beats / pk.issue.size();
    const long uram = pk.uram_grad + pk.uram_pos;
    printf("%-22s %8ld %7ld %9ld %6.2f%% %6.2f%% %6ld %7.2f%% %5ld %5zu %5zu %2d+%-2d=%2d %4ld %s %6.1fs %s\n",
           nl.name.c_str(), movable, fixed_slots, pins_in_scope, 100.0 * pins_17_100 / pins_all,
           100.0 * ((long)pk.beats.size() - pk.ideal_beats) / pk.ideal_beats, bubbles, 100.0 * efficiency,
           pk.colliding_nets, nl.offset_value[0].size(), nl.offset_value[1].size(),
           pk.node_bits, pk.offset_bits, pk.node_bits + pk.offset_bits,
           uram, uram > VC1902_URAMS ? "!" : " ", seconds, failures ? "FAIL" : "ok");
}

static void usage() {
    fprintf(stderr,
        "usage: beat_packer [--banks B] [--hazard H] [--window W] [--seed S] [--repair N]\n"
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
        if      (arg == "--banks")  cfg.banks  = std::stoi(next());
        else if (arg == "--hazard") cfg.hazard = std::stoi(next());
        else if (arg == "--window") cfg.window = std::stoi(next());
        else if (arg == "--seed")   cfg.seed   = (unsigned)std::stoul(next());
        else if (arg == "--repair") cfg.repair_passes = std::stoi(next());
        else if (arg == "--bookshelf" || arg == "--def") {
            std::string path = next(), name = next();
            designs.emplace_back(path, name); is_def.push_back(arg == "--def");
        } else usage();
    }
    if (designs.empty() || cfg.banks < 2 || cfg.banks > 64) usage();

    printf("banks=%d hazard=%d window=%d seed=%u   URAM = grad(movable) + pos(movable+fixed), %d floats/word, of %d\n",
           cfg.banks, cfg.hazard, cfg.window, cfg.seed, FLOATS_PER_WORD, VC1902_URAMS);
    printf("%-22s %8s %7s %9s %7s %7s %6s %8s %5s %5s %5s %8s %4s  %7s\n", "design", "movable", "fixed",
           "pins<=16", "pin>16", "packlos", "bubble", "effic", "coll", "offx", "offy", "bits n+o", "URAM", "time");
    int total_failures = 0;
    for (size_t d = 0; d < designs.size(); d++) {
        const auto start = std::chrono::steady_clock::now();
        Netlist nl = is_def[d] ? read_def(designs[d].first, designs[d].second)
                               : read_bookshelf(designs[d].first, designs[d].second);
        nl.nets.erase(std::remove_if(nl.nets.begin(), nl.nets.end(),
                                     [](const std::vector<Pin>& net) { return (int)net.size() > IGNORE_NET_DEGREE; }),
                      nl.nets.end());
        const auto expected = in_scope_geometry(nl);
        resolve_fixed_pins(nl);
        for (const auto& net : nl.nets) {
            std::vector<int> nodes;
            for (const Pin& pin : net)
                if (!std::count(nodes.begin(), nodes.end(), pin.node)) nodes.push_back(pin.node);
            nl.unique_nodes.push_back(std::move(nodes));
        }
        Packing pk;
        for (int n = 0; n < (int)nl.nets.size(); n++)
            if ((int)nl.nets[n].size() <= LANES) pk.in_scope_nets.push_back(n);
        std::mt19937 rng(cfg.seed);
        assign_banks(nl, cfg, pk);
        pack_beats(nl, cfg, pk, rng);
        schedule_beats(nl, cfg, pk);
        assign_node_slots(nl, cfg, pk);
        encode(nl, cfg, pk);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        int failures = check(nl, cfg, pk, expected);
        if (pk.colliding_nets) { fprintf(stderr, "  %s: %ld nets not encodable (own nodes share a bank)\n",
                                         nl.name.c_str(), pk.colliding_nets); failures++; }
        total_failures += failures;
        report(nl, pk, seconds, failures);
        fflush(stdout);
    }
    return total_failures ? 1 : 0;
}
