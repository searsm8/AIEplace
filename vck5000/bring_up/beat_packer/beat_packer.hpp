#ifndef BEAT_PACKER_HPP
#define BEAT_PACKER_HPP

// beat_packer.hpp -- host library: netlist -> static pin-record stream (#41). Header-only so the
// CLI (beat_packer.cpp) and the tier-1 harnesses share one implementation.
//
// Flow:  Netlist nl = read_bookshelf(...) / read_def(...) / built by a test
//        Encoded enc = encode_netlist(nl, cfg);        // nl is not modified
//        check(nl, enc, cfg)                           // decode the stream, compare geometry
//        slot_positions(enc, axis, node_pos)           // per-call position image, slot-major
//        macro_pin_refs(enc, axis)                     // on-chip refresh / fold list
//
// Node kinds after encode_netlist rewrites pins (the device only ever sees slots):
//   CELL       movable, own pins on nets              slot: position + gradient
//   MACRO      movable, pins rewritten to MACRO_PIN   slot: position + gradient (folded from its pins)
//   MACRO_PIN  one per distinct (macro, offset)       slot: position refreshed on chip, gradient folded out
//   FIXED      fixed, pins rewritten to FIXED_PIN     no slot
//   FIXED_PIN  one per distinct (fixed node, offset)  slot: constant absolute position, no gradient
// Rewriting every macro and fixed pin to its own slot keeps the offset tables to the cell
// library's pin geometry (tens to a few hundred values) instead of growing with macro pin count. Meow.

#include "pin_record.hpp"

#include <algorithm>
#include <array>
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

namespace packer {

using namespace pinrec;

constexpr int IGNORE_NET_DEGREE = 100;   // XPlace's mask (ignore_net_degree), dropped on the host. Meow.
constexpr int NO_BANK           = -1;
constexpr int BUBBLE            = -1;    // an issued all-EMPTY beat (hazard wait)
constexpr int NO_NET            = -1;
constexpr int URAM_WORDS        = 4096;  // URAM288: 4K x 72 bits, fixed shape
constexpr int FLOATS_PER_WORD   = 2;     // 64 of the 72 bits used
constexpr int VC1902_URAMS      = 463;

enum NodeKind : uint8_t { CELL, MACRO, MACRO_PIN, FIXED, FIXED_PIN };

struct Config {
    int      hazard        = HAZARD_DISTANCE;
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
    std::vector<char> movable;              // per node
    std::vector<char> is_macro;             // per node (only matters when movable)
    std::vector<std::vector<Pin>> nets;     // live nets, degree 2..IGNORE_NET_DEGREE
    std::vector<float> offset_value[2];     // per axis, distinct offset values
};

// Offset dictionary: one entry per distinct offset value (keyed on the float's bits). Meow.
struct OffsetDict {
    std::unordered_map<uint32_t, int> index;
    int key(float value, std::vector<float>& values) {
        uint32_t value_bits; std::memcpy(&value_bits, &value, sizeof value_bits);
        auto it = index.find(value_bits);
        if (it != index.end()) return it->second;
        index.emplace(value_bits, (int)values.size());
        values.push_back(value);
        return (int)values.size() - 1;
    }
};

inline bool in_scope(const std::vector<Pin>& net) { return (int)net.size() <= LANES; }

// ---------------------------------------------------------------------------------------------
// Parsers. Bookshelf (ISPD2005, MMS): "terminal"/"terminal_NI" in .nodes = fixed; a movable node
// taller than the most common movable height is a macro; pin offsets from .nets.
// DEF (ISPD2015): COMPONENTS "+ FIXED" = fixed; LEF CLASS BLOCK = macro; each "( PIN x )" is its
// own fixed IO node at offset 0; pin offsets from cells.lef next to the DEF, by the sw_only
// parser's rule (DataBase::lef_pin_cbk): center of the first RECT of the first PORT.

inline Netlist read_bookshelf(const std::string& dir, const std::string& name) {
    Netlist nl;
    const std::string suite_dir = dir.substr(0, dir.find_last_of('/'));
    nl.name = suite_dir.substr(suite_dir.find_last_of('/') + 1) + "/" + name;   // mms/ and ispd2005/ share names
    std::unordered_map<std::string, int> node_id;
    std::vector<double> height;
    std::ifstream nodes(dir + "/" + name + ".nodes");
    if (!nodes) { fprintf(stderr, "cannot open %s.nodes\n", name.c_str()); exit(2); }
    std::string line;
    while (std::getline(nodes, line)) {
        std::istringstream ss(line);
        std::string first; if (!(ss >> first)) continue;
        if (first[0] == '#' || first == "UCLA" || first == "NumNodes" || first == "NumTerminals") continue;
        double width = 0, node_height = 0; std::string kind; ss >> width >> node_height >> kind;
        node_id.emplace(first, (int)nl.movable.size());
        nl.movable.push_back(kind.rfind("terminal", 0) == 0 ? 0 : 1);
        height.push_back(node_height);
    }
    std::map<double, long> height_count;
    for (size_t n = 0; n < height.size(); n++) if (nl.movable[n]) height_count[height[n]]++;
    double row_height = 0; long best = -1;
    for (const auto& entry : height_count) if (entry.second > best) { best = entry.second; row_height = entry.first; }
    nl.is_macro.resize(height.size());
    for (size_t n = 0; n < height.size(); n++) nl.is_macro[n] = height[n] > row_height;

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
                pin.offset_key[axis] = dict[axis].key(std::stof(offset_text[axis]), nl.offset_value[axis]);
            cur.push_back(pin);
            if (--remaining == 0 && (int)cur.size() >= MIN_NET_DEGREE && (int)cur.size() <= IGNORE_NET_DEGREE)
                nl.nets.push_back(cur);
        }
    }
    if (parsed_nets != header_nets || parsed_pins != header_pins || remaining != 0) {
        fprintf(stderr, "%s: parsed %ld nets / %ld pins, header says %ld / %ld\n",
                name.c_str(), parsed_nets, parsed_pins, header_nets, header_pins);
        exit(2);
    }
    return nl;
}

struct LefMacro {
    bool block = false;
    std::unordered_map<std::string, std::array<float, 2>> pin_offset;
};

inline std::unordered_map<std::string, LefMacro> read_lef(const std::string& path) {
    std::unordered_map<std::string, LefMacro> macros;
    std::ifstream in(path);
    if (!in) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(2); }
    std::string line, macro, pin;
    bool want_rect = false;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string tok; if (!(ss >> tok)) continue;
        if (tok == "MACRO") { ss >> macro; pin.clear(); }
        else if (tok == "CLASS" && pin.empty() && !macro.empty()) { std::string cls; ss >> cls; macros[macro].block = cls == "BLOCK"; }
        else if (tok == "PIN") { ss >> pin; want_rect = false; }
        else if (tok == "PORT" && !pin.empty() && !macros[macro].pin_offset.count(pin)) want_rect = true;
        else if (tok == "RECT" && want_rect) {
            float x0, y0, x1, y1; ss >> x0 >> y0 >> x1 >> y1;
            macros[macro].pin_offset[pin] = {(x0 + x1) / 2.0f, (y0 + y1) / 2.0f};
            want_rect = false;
        } else if (tok == "END") {
            std::string what; ss >> what;
            if (what == pin) { pin.clear(); want_rect = false; }
        }
    }
    return macros;
}

inline Netlist read_def(const std::string& path, const std::string& name) {
    Netlist nl; nl.name = name;
    const auto lef = read_lef(path.substr(0, path.find_last_of('/')) + "/cells.lef");
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
                auto it = lef.find(comp_master);
                nl.is_macro.push_back(it != lef.end() && it->second.block);
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
                        nl.is_macro.push_back(0);
                    }
                    pin.node = it->second;
                } else {
                    auto it = node_id.find(comp);
                    if (it == node_id.end()) { fprintf(stderr, "unknown comp %s\n", comp.c_str()); exit(2); }
                    pin.node = it->second;
                    const std::array<float, 2>* lef_offset = nullptr;
                    auto macro = lef.find(master_of[comp]);
                    if (macro != lef.end()) {
                        auto lef_pin = macro->second.pin_offset.find(pin_name);
                        if (lef_pin != macro->second.pin_offset.end()) lef_offset = &lef_pin->second;
                    }
                    if (!lef_offset) { fprintf(stderr, "no LEF pin %s/%s\n", master_of[comp].c_str(), pin_name.c_str()); exit(2); }
                    offset = *lef_offset;
                }
                for (int axis = 0; axis < 2; axis++)
                    pin.offset_key[axis] = dict[axis].key(offset[axis], nl.offset_value[axis]);
                cur.push_back(pin);
            } else if (tok == ";" && in_stmt) {
                if ((int)cur.size() >= MIN_NET_DEGREE && (int)cur.size() <= IGNORE_NET_DEGREE) nl.nets.push_back(cur);
                in_stmt = false;
            }
        }
    }
    return nl;
}

// ---------------------------------------------------------------------------------------------
// Encoding.

struct Beat {
    int degree;
    std::vector<int> nets;
};

struct MacroPin {
    int pin_node;                   // MACRO_PIN node in Encoded::work
    int macro_node;
    float offset[2];
};

struct Encoded {
    Netlist work;                           // nl with macro and fixed pins rewritten to pin nodes
    std::vector<NodeKind> kind;             // per work node
    std::vector<int> base_node;             // per work node: itself, or the node a pin node stands for
    std::vector<std::array<float, 2>> pinned_offset;  // per work node: 0, or the offset a pin node bakes in
    std::vector<MacroPin> macro_pins;       // grouped by macro_node
    std::vector<std::vector<int>> unique_nodes;       // per net: distinct work nodes

    std::vector<int>  in_scope_nets;
    std::vector<int>  bank;                 // per work node, NO_BANK if it has no slot
    long              colliding_nets = 0;   // nets whose own nodes share a bank: not encodable
    std::vector<Beat> beats;
    long              ideal_beats = 0;      // sum over degrees of ceil(nets / nets_per_beat)
    std::vector<int>  issue;                // beat index per stream position, BUBBLE = all-EMPTY beat
    std::vector<int>  issue_degree;         // degree group of each stream position (bubbles included)

    std::vector<long> node_slot;            // per work node, -1 if none
    long              first_fixed_slot = 0;
    long              num_slots = 0;        // multiple of BANKS (and so of LANES)
    long              max_slot = 0;
    int               offset_bits = 0;
    bool              fits = false;         // max_slot below the EMPTY node field for offset_bits
    long              uram_grad = 0, uram_pos = 0;

    std::vector<uint32_t> records[2];       // per axis, LANES per stream position
    std::vector<int>  beat_count;           // [NET_DEGREES_PROCESSED] cumulative positions per degree
    std::vector<float> offset_table[2];     // per axis
    std::vector<int>  net_at;               // [position * MAX_NETS_PER_BEAT + k] -> nl net index or NO_NET
};

inline int bits_for(long max_value) {   // bits to represent 0..max_value
    int bits = 0;
    while ((1L << bits) <= max_value) bits++;
    return bits;
}

// Rewrite every pin of a fixed node or a movable macro to its own pin node, one per distinct
// (node, offset x, offset y). Offset tables then keep only offsets still used by CELL pins, with
// index 0 reserved for the zero offset every pin node uses. Meow.
inline void resolve_pin_nodes(const Netlist& nl, Encoded& enc) {
    Netlist& work = enc.work;
    work = nl;
    const int parsed_nodes = (int)nl.movable.size();
    enc.kind.resize(parsed_nodes);
    enc.base_node.resize(parsed_nodes);
    std::iota(enc.base_node.begin(), enc.base_node.end(), 0);
    enc.pinned_offset.assign(parsed_nodes, {0.0f, 0.0f});
    for (int n = 0; n < parsed_nodes; n++)
        enc.kind[n] = !nl.movable[n] ? FIXED : nl.is_macro[n] ? MACRO : CELL;

    std::map<std::array<int, 3>, int> pin_node;
    std::vector<char> used[2];
    for (int axis = 0; axis < 2; axis++) used[axis].assign(nl.offset_value[axis].size(), 0);
    for (auto& net : work.nets)
        for (Pin& pin : net) {
            const NodeKind owner = enc.kind[pin.node];
            if (owner == CELL) { for (int axis = 0; axis < 2; axis++) used[axis][pin.offset_key[axis]] = 1; continue; }
            auto it = pin_node.emplace(std::array<int, 3>{pin.node, pin.offset_key[0], pin.offset_key[1]},
                                       (int)work.movable.size()).first;
            if (it->second == (int)work.movable.size()) {
                const bool macro = owner == MACRO;
                work.movable.push_back(macro);
                work.is_macro.push_back(0);
                enc.kind.push_back(macro ? MACRO_PIN : FIXED_PIN);
                enc.base_node.push_back(pin.node);
                enc.pinned_offset.push_back({nl.offset_value[0][pin.offset_key[0]], nl.offset_value[1][pin.offset_key[1]]});
                if (macro) enc.macro_pins.push_back({it->second, pin.node,
                                                     {enc.pinned_offset.back()[0], enc.pinned_offset.back()[1]}});
            }
            pin.node = it->second;
            pin.offset_key[0] = pin.offset_key[1] = -1;   // zero offset, remapped below
        }
    std::stable_sort(enc.macro_pins.begin(), enc.macro_pins.end(),
                     [](const MacroPin& a, const MacroPin& b) { return a.macro_node < b.macro_node; });
    for (int axis = 0; axis < 2; axis++) {
        std::vector<int> remap(nl.offset_value[axis].size(), -1);
        std::vector<float> kept = {0.0f};
        for (size_t key = 0; key < remap.size(); key++)
            if (used[axis][key]) { remap[key] = (int)kept.size(); kept.push_back(nl.offset_value[axis][key]); }
        for (auto& net : work.nets)
            for (Pin& pin : net) pin.offset_key[axis] = pin.offset_key[axis] < 0 ? 0 : remap[pin.offset_key[axis]];
        work.offset_value[axis] = std::move(kept);
        enc.offset_table[axis] = work.offset_value[axis];
    }
    for (const auto& net : work.nets) {
        std::vector<int> nodes;
        for (const Pin& pin : net)
            if (!std::count(nodes.begin(), nodes.end(), pin.node)) nodes.push_back(pin.node);
        enc.unique_nodes.push_back(std::move(nodes));
    }
    for (int n = 0; n < (int)work.nets.size(); n++)
        if (in_scope(work.nets[n])) enc.in_scope_nets.push_back(n);
}

// Nodes that own a slot: everything movable (Nesterov updates cells and macros; macro pins carry
// the on-chip refresh/fold) plus fixed pin nodes. Parsed FIXED nodes are never referenced. Meow.
inline bool needs_slot(NodeKind kind) { return kind != FIXED; }
inline int slot_class(NodeKind kind) { return kind == FIXED_PIN ? 0 : 1; }   // 1 = movable rows

// Step 1: color slot-owning nodes into banks so each net's distinct nodes use distinct banks.
// Welsh-Powell order, least-loaded legal bank within the node's class (movable / fixed occupy
// separate row ranges, so each is balanced on its own), then min-conflicts repair. Nodes on no
// in-scope net take the least-loaded bank. Meow.
inline void assign_banks(Encoded& enc, const Config& cfg) {
    const Netlist& work = enc.work;
    const int num_nodes = (int)work.movable.size();
    std::vector<std::vector<int>> node_nets(num_nodes);
    for (int net : enc.in_scope_nets)
        for (int node : enc.unique_nodes[net]) node_nets[node].push_back(net);

    std::vector<int> order;
    std::vector<long> constraint_degree(num_nodes, 0);
    for (int node = 0; node < num_nodes; node++) {
        if (!needs_slot(enc.kind[node])) continue;
        for (int net : node_nets[node]) constraint_degree[node] += (long)enc.unique_nodes[net].size() - 1;
        order.push_back(node);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return constraint_degree[a] > constraint_degree[b]; });

    enc.bank.assign(num_nodes, NO_BANK);
    std::vector<long> bank_load[2] = {std::vector<long>(BANKS, 0), std::vector<long>(BANKS, 0)};
    for (int node : order) {
        auto& load = bank_load[slot_class(enc.kind[node])];
        uint64_t forbidden = 0;
        for (int net : node_nets[node])
            for (int other : enc.unique_nodes[net])
                if (enc.bank[other] != NO_BANK) forbidden |= 1ull << enc.bank[other];
        int best = -1;
        for (int k = 0; k < BANKS; k++)
            if (!(forbidden >> k & 1) && (best < 0 || load[k] < load[best])) best = k;
        if (best < 0) best = (int)(std::min_element(load.begin(), load.end()) - load.begin());
        enc.bank[node] = best;
        load[best]++;
    }

    std::vector<int> collisions(BANKS);
    for (int pass = 0; pass < cfg.repair_passes; pass++) {
        long moved = 0;
        for (int node : order) {
            if (node_nets[node].empty()) continue;
            auto& load = bank_load[slot_class(enc.kind[node])];
            std::fill(collisions.begin(), collisions.end(), 0);
            for (int net : node_nets[node])
                for (int other : enc.unique_nodes[net])
                    if (other != node) collisions[enc.bank[other]]++;
            const int current = enc.bank[node];
            if (collisions[current] == 0) continue;
            int best = current;
            for (int k = 0; k < BANKS; k++)
                if (collisions[k] < collisions[best] ||
                    (collisions[k] == collisions[best] && load[k] < load[best])) best = k;
            if (best == current) continue;
            load[current]--; load[best]++;
            enc.bank[node] = best;
            moved++;
        }
        if (moved == 0) break;
    }

    for (int net : enc.in_scope_nets) {
        uint64_t mask = 0;
        for (int node : enc.unique_nodes[net]) {
            if (mask >> enc.bank[node] & 1) { enc.colliding_nets++; break; }
            mask |= 1ull << enc.bank[node];
        }
    }
}

// Step 2: fill each beat with same-degree nets whose bank masks are pairwise disjoint. Same node
// => same bank, so this also keeps a node out of two nets of one beat. First-fit over a shuffled
// pool, at most `window` untaken nets ahead; a beat the window can't complete ships partly EMPTY. Meow.
inline void pack_beats(Encoded& enc, const Config& cfg, std::mt19937& rng) {
    std::vector<std::vector<int>> by_degree(LANES + 1);
    for (int net : enc.in_scope_nets) by_degree[enc.work.nets[net].size()].push_back(net);
    for (int degree = MIN_NET_DEGREE; degree <= LANES; degree++) {
        auto& pool = by_degree[degree];
        std::shuffle(pool.begin(), pool.end(), rng);
        const int nets_per_beat = LANES / degree;
        enc.ideal_beats += (pool.size() + nets_per_beat - 1) / nets_per_beat;
        std::vector<uint64_t> net_mask(pool.size(), 0);
        for (size_t i = 0; i < pool.size(); i++)
            for (int node : enc.unique_nodes[pool[i]]) net_mask[i] |= 1ull << enc.bank[node];
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
            enc.beats.push_back(std::move(beat));
        }
    }
}

// Step 3: order beats (degree groups contiguous and ascending -- resolve_beat's contract) so no
// movable node is re-updated within `hazard` positions; emit a BUBBLE when nothing in the window
// is ready. Fixed pin nodes are only read, so they carry no hazard. Meow.
inline void schedule_beats(Encoded& enc, const Config& cfg) {
    std::vector<long> last_update(enc.work.movable.size(), -(long)cfg.hazard);
    long position = 0;
    size_t group_begin = 0;
    while (group_begin < enc.beats.size()) {
        const int degree = enc.beats[group_begin].degree;
        size_t group_end = group_begin;
        while (group_end < enc.beats.size() && enc.beats[group_end].degree == degree) group_end++;
        std::deque<int> pending;
        for (size_t b = group_begin; b < group_end; b++) pending.push_back((int)b);
        while (!pending.empty()) {
            int pick = -1;
            for (int scan = 0; scan < (int)pending.size() && scan < cfg.window && pick < 0; scan++) {
                bool ready = true;
                for (int net : enc.beats[pending[scan]].nets)
                    for (int node : enc.unique_nodes[net])
                        if (enc.work.movable[node] && position - last_update[node] < cfg.hazard) ready = false;
                if (ready) pick = scan;
            }
            enc.issue_degree.push_back(degree);
            if (pick < 0) { enc.issue.push_back(BUBBLE); position++; continue; }
            const int b = pending[pick];
            pending.erase(pending.begin() + pick);
            for (int net : enc.beats[b].nets)
                for (int node : enc.unique_nodes[net]) if (enc.work.movable[node]) last_update[node] = position;
            enc.issue.push_back(b);
            position++;
        }
        group_begin = group_end;
    }
}

// Step 4: node_slot = row * BANKS + bank. Movable rows first in every bank, fixed rows after, so
// `node_slot >= first_fixed_slot` is the device's "read position, skip gradient write" test. Meow.
inline void assign_node_slots(Encoded& enc) {
    const auto& kind = enc.kind;
    std::vector<long> rows[2] = {std::vector<long>(BANKS, 0), std::vector<long>(BANKS, 0)};
    for (size_t node = 0; node < kind.size(); node++)
        if (enc.bank[node] != NO_BANK) rows[slot_class(kind[node])][enc.bank[node]]++;
    const long movable_rows = *std::max_element(rows[1].begin(), rows[1].end());
    const long fixed_rows   = *std::max_element(rows[0].begin(), rows[0].end());
    enc.first_fixed_slot = movable_rows * BANKS;
    enc.num_slots        = (movable_rows + fixed_rows) * BANKS;

    std::vector<long> next_row[2] = {std::vector<long>(BANKS, movable_rows), std::vector<long>(BANKS, 0)};
    enc.node_slot.assign(kind.size(), -1);
    for (size_t node = 0; node < kind.size(); node++) {
        const int bank = enc.bank[node];
        if (bank == NO_BANK) continue;
        enc.node_slot[node] = next_row[slot_class(kind[node])][bank]++ * BANKS + bank;
        enc.max_slot = std::max(enc.max_slot, enc.node_slot[node]);
    }
    const long words_per_uram = (long)URAM_WORDS * FLOATS_PER_WORD;
    enc.uram_grad = BANKS * ((movable_rows + words_per_uram - 1) / words_per_uram);
    enc.uram_pos  = BANKS * ((movable_rows + fixed_rows + words_per_uram - 1) / words_per_uram);
}

// Step 5: emit the per-axis record streams. Net k of a beat occupies lanes [k*d, (k+1)*d); its pins
// are sorted by node_slot so a node's repeated pins are adjacent; every other lane is EMPTY. Meow.
inline void encode_records(Encoded& enc) {
    long max_offset_idx = 0;
    for (int axis = 0; axis < 2; axis++) max_offset_idx = std::max(max_offset_idx, (long)enc.offset_table[axis].size() - 1);
    enc.offset_bits = bits_for(max_offset_idx);
    enc.fits = enc.offset_bits < 32 && (uint64_t)enc.max_slot <= max_real_node_slot(enc.offset_bits);
    if (!enc.fits) return;
    for (int axis = 0; axis < 2; axis++) enc.records[axis].assign(enc.issue.size() * LANES, EMPTY_RECORD);
    enc.net_at.assign(enc.issue.size() * MAX_NETS_PER_BEAT, NO_NET);
    for (size_t position = 0; position < enc.issue.size(); position++) {
        if (enc.issue[position] == BUBBLE) continue;
        const Beat& beat = enc.beats[enc.issue[position]];
        for (size_t k = 0; k < beat.nets.size(); k++) {
            enc.net_at[position * MAX_NETS_PER_BEAT + k] = beat.nets[k];
            std::vector<Pin> pins = enc.work.nets[beat.nets[k]];
            std::stable_sort(pins.begin(), pins.end(),
                             [&](const Pin& a, const Pin& b) { return enc.node_slot[a.node] < enc.node_slot[b.node]; });
            for (int p = 0; p < beat.degree; p++)
                for (int axis = 0; axis < 2; axis++)
                    enc.records[axis][position * LANES + k * beat.degree + p] =
                        make_record((uint32_t)enc.node_slot[pins[p].node], (uint32_t)pins[p].offset_key[axis], enc.offset_bits);
        }
    }
    enc.beat_count.assign(NET_DEGREES_PROCESSED, 0);
    for (int degree : enc.issue_degree)
        for (int i = degree - MIN_NET_DEGREE; i < NET_DEGREES_PROCESSED; i++) enc.beat_count[i]++;
}

// Steps 1-5 on an Encoded whose work netlist, kinds, pin-node maps, unique_nodes and in_scope_nets
// are already set -- by resolve_pin_nodes for a whole design, or by encode_chunked for one chunk. Meow.
inline void encode_resolved(Encoded& enc, const Config& cfg) {
    std::mt19937 rng(cfg.seed);
    assign_banks(enc, cfg);
    pack_beats(enc, cfg, rng);
    schedule_beats(enc, cfg);
    assign_node_slots(enc);
    encode_records(enc);
}

inline Encoded encode_netlist(const Netlist& nl, const Config& cfg) {
    Encoded enc;
    resolve_pin_nodes(nl, enc);
    encode_resolved(enc, cfg);
    return enc;
}

// ---------------------------------------------------------------------------------------------
// Device-facing arrays built per call from node positions (indexed by PARSED node).

// Slot-major position image for one axis. CELL/MACRO slots carry the node position; FIXED_PIN
// slots carry node position + offset (constant); MACRO_PIN slots are NaN on purpose -- the device
// must refresh them from the macro, and a missed refresh then poisons the result visibly. Meow.
inline std::vector<float> slot_positions(const Encoded& enc, int axis, const std::vector<float>& node_pos) {
    std::vector<float> image(enc.num_slots, 0.0f);
    for (size_t node = 0; node < enc.kind.size(); node++) {
        if (enc.node_slot[node] < 0) continue;
        float value = node_pos[enc.base_node[node]];
        if (enc.kind[node] == FIXED_PIN) value += enc.pinned_offset[node][axis];
        if (enc.kind[node] == MACRO_PIN) value = NAN;
        image[enc.node_slot[node]] = value;
    }
    return image;
}

inline std::vector<MacroPinRef> macro_pin_refs(const Encoded& enc, int axis) {
    std::vector<MacroPinRef> refs;
    for (const MacroPin& mp : enc.macro_pins)
        refs.push_back({(int32_t)enc.node_slot[mp.pin_node], (int32_t)enc.node_slot[mp.macro_node], mp.offset[axis]});
    return refs;
}

// ---------------------------------------------------------------------------------------------
// Independent checker: decodes the record streams using only what the device sees (records,
// beat_count, offset_bits, first_fixed_slot, offset tables) plus the slot->node inverse, and
// requires the decoded geometry -- (parsed node, total offset) per pin -- to equal the in-scope
// nets of the PARSED netlist, so a wrong pin-node rewrite is caught too. Every beat must also
// satisfy the bank, hazard and adjacency rules. Meow.

using GeomPin = std::tuple<int, float, float>;

inline std::vector<std::vector<GeomPin>> in_scope_geometry(const Netlist& nl) {
    std::vector<std::vector<GeomPin>> nets;
    for (const auto& net : nl.nets) {
        if (!in_scope(net)) continue;
        std::vector<GeomPin> geom;
        for (const Pin& pin : net)
            geom.emplace_back(pin.node, nl.offset_value[0][pin.offset_key[0]], nl.offset_value[1][pin.offset_key[1]]);
        std::sort(geom.begin(), geom.end());
        nets.push_back(std::move(geom));
    }
    std::sort(nets.begin(), nets.end());
    return nets;
}

struct Failures {
    int count = 0;
    void operator()(const char* what, long where) {
        if (count++ < 10) fprintf(stderr, "  CHECK FAIL: %s (at %ld)\n", what, where);
    }
};

// Decode one stream to geometry (appended to `decoded`, unsorted) and check its per-beat rules. Meow.
inline void decode_stream(const Encoded& enc, const Config& cfg, Failures& fail,
                          std::vector<std::vector<GeomPin>>& decoded) {
    if (!enc.fits) { fail("record does not fit 32 bits", enc.max_slot); return; }
    if (enc.colliding_nets) fail("nets whose own nodes share a bank", enc.colliding_nets);
    const int ob = enc.offset_bits;
    const uint32_t empty_slot = record_node_slot(EMPTY_RECORD, ob);
    std::unordered_map<uint32_t, int> slot_to_node;
    for (size_t node = 0; node < enc.node_slot.size(); node++)
        if (enc.node_slot[node] >= 0 && !slot_to_node.emplace((uint32_t)enc.node_slot[node], (int)node).second)
            fail("two nodes share a node_slot", enc.node_slot[node]);
    if (enc.num_slots % LANES) fail("num_slots not a multiple of LANES", enc.num_slots);

    std::unordered_map<uint32_t, long> last_update;
    const long num_positions = (long)enc.records[0].size() / LANES;
    for (long position = 0; position < num_positions; position++) {
        int degree = MIN_NET_DEGREE;   // resolve_beat's rule, from cumulative counts
        for (int count : enc.beat_count) if (position >= count) degree++;
        const int nets_per_beat = LANES / degree;
        std::vector<uint32_t> beat_slots;
        for (int lane = 0; lane < LANES; lane++) {
            const uint32_t rx = enc.records[0][position * LANES + lane], ry = enc.records[1][position * LANES + lane];
            if (record_node_slot(rx, ob) != record_node_slot(ry, ob)) fail("x and y node_slot differ", position);
            if (lane >= nets_per_beat * degree && rx != EMPTY_RECORD) fail("tail lane not EMPTY", position);
        }
        for (int k = 0; k < nets_per_beat; k++) {
            const long base = position * LANES + (long)k * degree;
            int empty_lanes = 0;
            for (int p = 0; p < degree; p++) empty_lanes += enc.records[0][base + p] == EMPTY_RECORD;
            if (empty_lanes == degree) continue;
            if (empty_lanes) { fail("net partly EMPTY", position); continue; }
            std::vector<GeomPin> net;
            std::vector<uint32_t> net_slots;
            for (int p = 0; p < degree; p++) {
                const uint32_t slot = record_node_slot(enc.records[0][base + p], ob);
                auto it = slot_to_node.find(slot);
                if (slot == empty_slot || it == slot_to_node.end()) { fail("node_slot maps to no node", position); continue; }
                if (!net_slots.empty() && net_slots.back() != slot &&
                    std::count(net_slots.begin(), net_slots.end(), slot)) fail("repeated node not adjacent", position);
                if (net_slots.empty() || net_slots.back() != slot) net_slots.push_back(slot);
                const int node = it->second;
                const uint32_t idx_x = record_offset_idx(enc.records[0][base + p], ob);
                const uint32_t idx_y = record_offset_idx(enc.records[1][base + p], ob);
                if (idx_x >= enc.offset_table[0].size() || idx_y >= enc.offset_table[1].size()) {
                    fail("offset_idx past table end", position); continue;
                }
                net.emplace_back(enc.base_node[node], enc.pinned_offset[node][0] + enc.offset_table[0][idx_x],
                                                      enc.pinned_offset[node][1] + enc.offset_table[1][idx_y]);
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
            const int bank = (int)(slot % BANKS);
            if (banks_used >> bank & 1) fail("bank hit twice in one beat", position);
            banks_used |= 1ull << bank;
            if (slot < enc.first_fixed_slot) {
                auto it = last_update.find(slot);
                if (it != last_update.end() && position - it->second < cfg.hazard) fail("RAW hazard", position);
                last_update[slot] = position;
            }
        }
    }
    for (const auto& entry : slot_to_node)
        if ((entry.first < enc.first_fixed_slot) != (bool)enc.work.movable[entry.second]) fail("movable/fixed slot range wrong", entry.first);
    for (const MacroPin& mp : enc.macro_pins)
        if (enc.base_node[mp.pin_node] != enc.base_node[mp.macro_node]) fail("macro pin list names the wrong macro", mp.pin_node);
}

inline int check(const Netlist& nl, const Encoded& enc, const Config& cfg) {
    Failures fail;
    std::vector<std::vector<GeomPin>> decoded;
    decode_stream(enc, cfg, fail, decoded);
    if (!enc.fits) return fail.count;
    for (size_t node = 0; node < nl.movable.size(); node++)
        if (nl.movable[node] && enc.node_slot[node] < 0) fail("movable node without a slot", (long)node);
    std::sort(decoded.begin(), decoded.end());
    if (decoded != in_scope_geometry(nl)) fail("decoded netlist differs from input", (long)decoded.size());
    return fail.count;
}

// ---------------------------------------------------------------------------------------------
// Chunking: when the slots exceed on-chip capacity, split the design into K chunks that each fit.
//
// Every movable node is OWNED by one chunk (a macro and its macro pins share one). Every in-scope
// net is HOMED in the chunk that owns most of its movable nodes; the chunk's stream carries its
// homed nets. A homed net's node owned elsewhere becomes a GHOST slot in the home chunk; fixed pin
// nodes are simply copied into every chunk that uses them (their position never changes).
// Each chunk is then an ordinary Encoded stream over local slots, so node_slot bits shrink too.
//
// Ghosts move through one DDR exchange buffer laid out consumer-major: chunk k's REGION holds its
// ghosts, as one BLOCK per producer j (ascending j), each block ordered by the producer's slot.
//   positions: producer j, resident, writes its K blocks (sequential within each block);
//              consumer k reads its one region sequentially into its ghost slots.
//   gradients: consumer k writes its ghost gradients back into its region, same order;
//              producer j reads its K blocks and adds them into its own slots.
// Every DDR access is sequential; the random side is always the on-chip URAM. Meow.

struct ExchangeBlock { long offset; long count; };

struct Chunk {
    Encoded enc;                             // local: enc.work node ids are chunk-local
    std::vector<int>  net_global;            // local net -> global net (= parsed net index)
    std::vector<int>  work_node;             // local node -> global work node
    std::vector<char> ghost;                 // per local node
    long region_offset = 0;                  // this chunk's ghost region in the exchange buffer
    std::vector<int>  import_local;          // ghost local nodes, region order
    std::vector<ExchangeBlock> export_blocks;   // [K]: where this chunk's exports land, per consumer
    std::vector<int>  export_local;          // own local nodes, export_blocks order
};

struct Chunked {
    Encoded global;                          // resolved work netlist (not encoded)
    long capacity = 0;
    int  num_chunks = 0;
    std::vector<int> owner;                  // per global work node, -1 for fixed
    std::vector<int> home;                   // per global net, -1 if out of scope
    std::vector<Chunk> chunks;
    long exchange_size = 0;
    long ghosts = 0;
    bool fits = false;
};

// Locality order over movable nodes: breadth-first over in-scope nets, a macro and its pins as one
// unit, restarting at the lowest unvisited id. Contiguous runs of it become chunks. Meow.
inline std::vector<int> locality_order(const Encoded& g) {
    const int num_nodes = (int)g.kind.size();
    std::vector<std::vector<int>> node_nets(num_nodes);
    for (int net : g.in_scope_nets)
        for (int node : g.unique_nodes[net]) node_nets[node].push_back(net);
    std::vector<std::vector<int>> macro_pins_of(num_nodes);
    for (const MacroPin& mp : g.macro_pins) macro_pins_of[mp.macro_node].push_back(mp.pin_node);
    auto unit_of = [&](int node) { return g.kind[node] == MACRO_PIN ? g.base_node[node] : node; };

    std::vector<char> seen(num_nodes, 0);
    std::vector<int> order;
    std::deque<int> queue;
    for (int start = 0; start < num_nodes; start++) {
        if (!g.work.movable[start] || seen[unit_of(start)]) continue;
        queue.push_back(unit_of(start)); seen[unit_of(start)] = 1;
        while (!queue.empty()) {
            const int unit = queue.front(); queue.pop_front();
            std::vector<int> members = {unit};
            members.insert(members.end(), macro_pins_of[unit].begin(), macro_pins_of[unit].end());
            for (int member : members) {
                order.push_back(member);
                for (int net : node_nets[member])
                    for (int other : g.unique_nodes[net]) {
                        if (!g.work.movable[other]) continue;
                        const int other_unit = unit_of(other);
                        if (!seen[other_unit]) { seen[other_unit] = 1; queue.push_back(other_unit); }
                    }
            }
        }
    }
    return order;
}

inline bool build_chunks(Chunked& ch, const std::vector<int>& order, int num_chunks, const Config& cfg) {
    const Encoded& g = ch.global;
    const int num_nodes = (int)g.kind.size();
    ch.num_chunks = num_chunks;
    ch.owner.assign(num_nodes, -1);
    const long per_chunk = ((long)order.size() + num_chunks - 1) / num_chunks;
    for (size_t i = 0; i < order.size(); i++) ch.owner[order[i]] = std::min<long>((long)i / per_chunk, num_chunks - 1);
    for (const MacroPin& mp : g.macro_pins) ch.owner[mp.pin_node] = ch.owner[mp.macro_node];   // same unit

    ch.home.assign(g.work.nets.size(), -1);
    std::vector<long> votes(num_chunks);
    for (int net : g.in_scope_nets) {
        std::fill(votes.begin(), votes.end(), 0);
        for (int node : g.unique_nodes[net]) if (ch.owner[node] >= 0) votes[ch.owner[node]]++;
        ch.home[net] = (int)(std::max_element(votes.begin(), votes.end()) - votes.begin());
    }

    ch.chunks.assign(num_chunks, Chunk());
    ch.ghosts = 0;
    for (int k = 0; k < num_chunks; k++) {
        Chunk& c = ch.chunks[k];
        std::vector<int> local_of(num_nodes, -1);
        auto add_node = [&](int node, bool is_ghost) {
            local_of[node] = (int)c.work_node.size();
            c.work_node.push_back(node);
            c.ghost.push_back(is_ghost);
        };
        for (int node = 0; node < num_nodes; node++) if (ch.owner[node] == k) add_node(node, false);
        std::vector<int> nets;
        for (int net : g.in_scope_nets) if (ch.home[net] == k) nets.push_back(net);
        for (int net : nets)
            for (int node : g.unique_nodes[net])
                if (local_of[node] < 0) add_node(node, g.work.movable[node]);   // fixed pins are copies, not ghosts

        Encoded& e = c.enc;
        e.work.name = g.work.name;
        for (int node : c.work_node) {
            e.work.movable.push_back(g.work.movable[node]);
            e.work.is_macro.push_back(0);
            e.kind.push_back(g.kind[node]);
            e.base_node.push_back(g.base_node[node]);
            e.pinned_offset.push_back(g.pinned_offset[node]);
        }
        for (int axis = 0; axis < 2; axis++) { e.work.offset_value[axis] = g.work.offset_value[axis]; e.offset_table[axis] = g.offset_table[axis]; }
        for (int net : nets) {
            std::vector<Pin> local_net = g.work.nets[net];
            for (Pin& pin : local_net) pin.node = local_of[pin.node];
            std::vector<int> local_unique;
            for (int node : g.unique_nodes[net]) local_unique.push_back(local_of[node]);
            e.in_scope_nets.push_back((int)e.work.nets.size());
            c.net_global.push_back(net);
            e.work.nets.push_back(std::move(local_net));
            e.unique_nodes.push_back(std::move(local_unique));
        }
        for (const MacroPin& mp : g.macro_pins)
            if (ch.owner[mp.macro_node] == k)
                e.macro_pins.push_back({local_of[mp.pin_node], local_of[mp.macro_node], {mp.offset[0], mp.offset[1]}});
        encode_resolved(e, cfg);
        if (!e.fits || e.num_slots > ch.capacity) return false;
        for (char is_ghost : c.ghost) ch.ghosts += is_ghost;
    }

    // Exchange layout: region per consumer, block per producer, block ordered by producer slot.
    std::vector<std::vector<int>> local_in(num_chunks, std::vector<int>(num_nodes, -1));
    for (int k = 0; k < num_chunks; k++)
        for (size_t l = 0; l < ch.chunks[k].work_node.size(); l++) local_in[k][ch.chunks[k].work_node[l]] = (int)l;
    for (int j = 0; j < num_chunks; j++) ch.chunks[j].export_blocks.assign(num_chunks, ExchangeBlock{0, 0});
    long offset = 0;
    for (int k = 0; k < num_chunks; k++) {
        Chunk& consumer = ch.chunks[k];
        consumer.region_offset = offset;
        for (int j = 0; j < num_chunks; j++) {
            std::vector<int> block;   // global work nodes
            for (size_t l = 0; l < consumer.work_node.size(); l++)
                if (consumer.ghost[l] && ch.owner[consumer.work_node[l]] == j) block.push_back(consumer.work_node[l]);
            const Encoded& producer = ch.chunks[j].enc;
            std::sort(block.begin(), block.end(), [&](int a, int b) {
                return producer.node_slot[local_in[j][a]] < producer.node_slot[local_in[j][b]]; });
            ch.chunks[j].export_blocks[k] = ExchangeBlock{offset, (long)block.size()};
            for (int node : block) consumer.import_local.push_back(local_in[k][node]);
            offset += (long)block.size();
        }
    }
    // Producer j's export list is its blocks in consumer order: walking every region in order and
    // keeping the entries j owns gives exactly that. Meow.
    for (int k = 0; k < num_chunks; k++)
        for (int local_ghost : ch.chunks[k].import_local) {
            const int node = ch.chunks[k].work_node[local_ghost];
            ch.chunks[ch.owner[node]].export_local.push_back(local_in[ch.owner[node]][node]);
        }
    ch.exchange_size = offset;
    return true;
}

inline Chunked encode_chunked(const Netlist& nl, const Config& cfg, long capacity) {
    Chunked ch;
    ch.capacity = capacity;
    resolve_pin_nodes(nl, ch.global);
    const std::vector<int> order = locality_order(ch.global);
    long slot_owners = 0;
    for (size_t node = 0; node < ch.global.kind.size(); node++) slot_owners += needs_slot(ch.global.kind[node]);
    for (int num_chunks = std::max<long>(1, (slot_owners + capacity - 1) / capacity); num_chunks <= 64; num_chunks++)
        if (build_chunks(ch, order, num_chunks, cfg)) { ch.fits = true; return ch; }
    return ch;
}

// Slot-major position image of chunk k, one axis. Own cells/macros carry their position, fixed
// pins position + offset; macro pins AND ghosts are NaN -- the device must refresh / import them. Meow.
inline std::vector<float> chunk_slot_positions(const Chunked& ch, int k, int axis, const std::vector<float>& node_pos) {
    const Chunk& c = ch.chunks[k];
    std::vector<float> image = slot_positions(c.enc, axis, node_pos);
    for (size_t l = 0; l < c.ghost.size(); l++) if (c.ghost[l]) image[c.enc.node_slot[l]] = NAN;
    return image;
}

// Every per-chunk array the device reads, for one axis, concatenated in chunk order and located
// by the descriptors. All chunks share the global offset tables, hence one offset_bits. Meow.
struct ChunkedDevice {
    std::vector<ChunkDesc>        desc;
    std::vector<uint32_t>         records;        // LANES per beat
    std::vector<float>            slot_images;    // slot-major, each chunk a whole number of beats
    std::vector<MacroPinRef>      macro_pins;
    std::vector<int32_t>          import_slots;   // ghost local slots, region order
    std::vector<int32_t>          export_slots;   // own local slots, block order
    std::vector<ExchangeBlockRef> blocks;         // num_chunks per chunk
    std::vector<float>            offset_table;
    int                           offset_bits = 0;
    long                          exchange_size = 0;
};

inline ChunkedDevice chunked_device_arrays(const Chunked& ch, int axis, const std::vector<float>& node_pos) {
    ChunkedDevice dev;
    dev.offset_table  = ch.global.offset_table[axis];
    dev.offset_bits   = ch.chunks[0].enc.offset_bits;
    dev.exchange_size = ch.exchange_size;
    for (int k = 0; k < ch.num_chunks; k++) {
        const Chunk& c = ch.chunks[k];
        if (c.enc.offset_bits != dev.offset_bits) { fprintf(stderr, "chunks disagree on offset_bits\n"); exit(2); }
        ChunkDesc d = {};
        d.record_beat_offset = (int32_t)(dev.records.size() / LANES);
        d.num_beats          = (int32_t)c.enc.issue.size();
        for (int i = 0; i < NET_DEGREES_PROCESSED; i++) d.beat_count[i] = c.enc.beat_count[i];
        d.slot_beat_offset   = (int32_t)(dev.slot_images.size() / LANES);
        d.num_slot_beats     = (int32_t)(c.enc.num_slots / LANES);
        d.first_fixed_slot   = (int32_t)c.enc.first_fixed_slot;
        d.macro_pin_offset   = (int32_t)dev.macro_pins.size();
        d.num_macro_pins     = (int32_t)c.enc.macro_pins.size();
        d.import_region      = (int32_t)c.region_offset;
        d.num_imports        = (int32_t)c.import_local.size();
        d.import_list_offset = (int32_t)dev.import_slots.size();
        d.export_list_offset = (int32_t)dev.export_slots.size();
        d.export_block_offset = (int32_t)dev.blocks.size();
        dev.desc.push_back(d);

        dev.records.insert(dev.records.end(), c.enc.records[axis].begin(), c.enc.records[axis].end());
        const std::vector<float> image = chunk_slot_positions(ch, k, axis, node_pos);
        dev.slot_images.insert(dev.slot_images.end(), image.begin(), image.end());
        const std::vector<MacroPinRef> refs = macro_pin_refs(c.enc, axis);
        dev.macro_pins.insert(dev.macro_pins.end(), refs.begin(), refs.end());
        for (int local : c.import_local) dev.import_slots.push_back((int32_t)c.enc.node_slot[local]);
        for (int local : c.export_local) dev.export_slots.push_back((int32_t)c.enc.node_slot[local]);
        for (const ExchangeBlock& block : c.export_blocks) dev.blocks.push_back({(int32_t)block.offset, (int32_t)block.count});
    }
    return dev;
}

// Chunked checker: every chunk decodes on its own (local slots -> parsed geometry); the union must
// be the in-scope netlist exactly. Plus ownership, capacity, and the exchange: every buffer entry
// written once, by the owner of the node the consumer expects, and each producer's gradient fold
// sequence keeps a slot `hazard` entries apart. Meow.
inline int check_chunked(const Netlist& nl, const Chunked& ch, const Config& cfg) {
    Failures fail;
    if (!ch.fits) { fail("no chunk count fits the capacity", ch.capacity); return fail.count; }
    std::vector<std::vector<GeomPin>> decoded;
    std::vector<int> own_count(ch.global.kind.size(), 0);
    for (int k = 0; k < ch.num_chunks; k++) {
        const Chunk& c = ch.chunks[k];
        decode_stream(c.enc, cfg, fail, decoded);
        if (c.enc.num_slots > ch.capacity) fail("chunk exceeds capacity", k);
        for (size_t l = 0; l < c.work_node.size(); l++) {
            const int node = c.work_node[l];
            if (!c.ghost[l] && ch.global.work.movable[node]) { own_count[node]++; if (ch.owner[node] != k) fail("own node of another chunk", node); }
            if (c.ghost[l] && ch.owner[node] == k) fail("ghost of an own node", node);
            if (c.enc.node_slot[l] < 0) fail("local node without a slot", node);
        }
    }
    for (size_t node = 0; node < own_count.size(); node++)
        if (needs_slot(ch.global.kind[node]) && ch.global.work.movable[node] && own_count[node] != 1) fail("movable node not owned exactly once", (long)node);
    for (const MacroPin& mp : ch.global.macro_pins)
        if (ch.owner[mp.pin_node] != ch.owner[mp.macro_node]) fail("macro pin split from its macro", mp.pin_node);

    std::vector<int> written_node(ch.exchange_size, -1), writes(ch.exchange_size, 0);
    for (int j = 0; j < ch.num_chunks; j++) {
        const Chunk& p = ch.chunks[j];
        size_t e = 0;
        std::vector<long> fold_slots;
        for (int k = 0; k < ch.num_chunks; k++)
            for (long i = 0; i < p.export_blocks[k].count; i++, e++) {
                if (e >= p.export_local.size()) { fail("export list shorter than its blocks", j); break; }
                const long position = p.export_blocks[k].offset + i;
                writes[position]++;
                written_node[position] = p.work_node[p.export_local[e]];
                if (p.ghost[p.export_local[e]]) fail("export of a ghost", j);
                fold_slots.push_back(p.enc.node_slot[p.export_local[e]]);
            }
        if (e != p.export_local.size()) fail("export list longer than its blocks", j);
        for (size_t a = 0; a < fold_slots.size(); a++)
            for (size_t b = a + 1; b < fold_slots.size() && b < a + (size_t)cfg.hazard; b++)
                if (fold_slots[a] == fold_slots[b]) fail("producer fold RAW hazard", j);
    }
    for (long position = 0; position < ch.exchange_size; position++) if (writes[position] != 1) fail("exchange entry not written exactly once", position);
    for (int k = 0; k < ch.num_chunks; k++) {
        const Chunk& c = ch.chunks[k];
        long ghosts = 0;
        for (char is_ghost : c.ghost) ghosts += is_ghost;
        if ((long)c.import_local.size() != ghosts) fail("import list does not cover every ghost", k);
        for (size_t i = 0; i < c.import_local.size(); i++) {
            const long position = c.region_offset + (long)i;
            if (position >= ch.exchange_size || written_node[position] != c.work_node[c.import_local[i]]) fail("ghost imports the wrong node", k);
        }
    }
    std::sort(decoded.begin(), decoded.end());
    if (decoded != in_scope_geometry(nl)) fail("decoded chunks differ from the input netlist", (long)decoded.size());
    return fail.count;
}

} // namespace packer

#endif // BEAT_PACKER_HPP
