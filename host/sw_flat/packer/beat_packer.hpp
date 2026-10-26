#ifndef BEAT_PACKER_HPP
#define BEAT_PACKER_HPP

// beat_packer.hpp -- host library: netlist -> static pin-record stream (#41). Header-only so the
// CLI (beat_packer.cpp) and the tier-1 harnesses share one implementation.
// Copied 2026-10-06 (#45) with partition.hpp from vck5000/bring_up/beat_packer/ @ 4bf0baf; this is
// now the live copy, the vck5000 one is read-only legacy. Meow.
//
// Flow:  Netlist nl = read_netlist(file) / built by a test   // file: host/sw_flat export_netlist
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
//
// =================================================================================================
// THE PACKER'S CONTRACT -- every rule the device modules rely on and do NOT check themselves.
// A device module that relies on a rule should cite its tag (e.g. "packer rule B4"). Each rule says
// who verifies it: check() / check_chunked() decode the streams with only what the device sees;
// "harness" means only a device harness would notice (a wrong result), not the checker.
// A new device assumption is not real until it is a rule here AND the checker enforces it. Meow.
//
// Records (pin_record.hpp)
//  R1  record = node_slot << offset_bits | offset_idx. The x and y streams are identical except
//      for offset_idx: same length, same node_slot in every lane.                  [check]
//  R2  EMPTY_RECORD (all ones) marks a lane with no pin. Every real node_slot is
//      <= max_real_node_slot(offset_bits), so no real record can equal it.          [check: fits]
//  R3  offset_idx indexes this axis's offset table; entry 0 is 0.0f (every pin node uses it).
//                                                                [check: range; harness: value]
// Slots
//  S1  node_slot = row * BANKS + bank, one distinct slot per slot-owning node.         [check]
//  S2  Movable slots (CELL, MACRO, MACRO_PIN) lie below first_fixed_slot; FIXED_PIN slots at or
//      above it. first_fixed_slot and num_slots are multiples of BANKS (so of LANES): the device
//      tests "fixed" as node_slot >= first_fixed_slot and loads/drains whole beats.   [check]
//  S3  Position image (slot_positions): CELL/MACRO slots hold the node position, FIXED_PIN slots
//      position + offset, MACRO_PIN slots NaN (the device must refresh them).     [harness: NaN]
//  S4  num_slots <= the device's slot capacity; otherwise the design must be chunked (C rules).
//                                                                      [harness / check_chunked]
// Small nets (degree 2..16), stream positions [0, degree_group_end[NET_DEGREES_PROCESSED-1])
//  B1  Beats are grouped by degree, ascending and contiguous; degree_group_end[d-2] is the cumulative
//      count through degree d (bubbles included), which is how the device finds a beat's degree.
//                                                                          [check: decode rule]
//  B2  Net k of a degree-d beat occupies lanes [k*d, (k+1)*d) and is wholly live or wholly EMPTY;
//      lanes from nets_per_beat*d up are EMPTY. The device calls net k live iff lane k*d holds a
//      pin.                                                                            [check]
//  B3  A node's repeated pins on one net sit in adjacent lanes (pins sorted by node_slot); the
//      device merges equal adjacent slots before the scatter.                          [check]
//  B4  Within a beat, distinct nodes use distinct banks, across all its nets, so each bank serves
//      one row per beat; and a node appears in at most one net of a beat.              [check]
//  B5  A movable node_slot is not touched again within HAZARD_DISTANCE stream positions (the
//      scatter-add read-modify-write). A BUBBLE (all-EMPTY beat) is a position too.    [check]
// Large nets (degree 17..96, cfg.large_nets only), positions [degree_group_end[last], total)
//  L1  Follow the small nets, grouped by span 2..MAX_SPAN, ascending and contiguous;
//      span_group_end[s-2] is cumulative through span s and continues degree_group_end. Its last entry
//      equals the stream length, even with large nets off (then every entry equals it).  [check]
//  L2  One net per beat; a net's `span` real beats are consecutive apart from pads (L7), so the
//      device finds net boundaries with a counter of real beats.            [check: decode rule]
//  L3  A real beat's pins start at lane 0 and EMPTY lanes only trail. The device substitutes lane
//      0's position into EMPTY lanes.                                                  [check]
//  L4  Within a beat: distinct nodes use distinct banks, and a node's pins are adjacent. A node
//      with > LANES pins on the net is split into <= LANES-pin runs in different beats (same
//      bank, so never in one beat).                                                    [check]
//  L5  Large nets are degree 17..MAX_LARGE_NET_DEGREE (96). Nets over IGNORE_NET_DEGREE (= 96 since
//      2026-10-06, #46; XPlace uses 100) never reach the packer (read-time mask). Nets needing
//      > MAX_SPAN beats are dropped and counted in large_dropped. [check: geometry vs enc.large_nets]
//  L6  B5's hazard rule holds here too, continuing from the small-net section: a movable node_slot
//      is not touched again within HAZARD_DISTANCE stream positions, pads included. Met by list-
//      scheduling nets within a span group and a net's beats within the net, padding only when
//      nothing is ready (a node split over beats of one net always needs pads).          [check]
//  L7  A PAD is an all-EMPTY beat in this section, recognised by lane 0 being EMPTY (a real beat
//      never has an EMPTY lane 0, L3). Pads are invisible to net accounting: the device neither
//      counts them toward a net's span nor shifts them into its window. A pad may sit between
//      nets or inside one; its position counts in span_group_end for the group it sits in.
//                                                                                      [check]
//  L8  A net's EXTENT -- positions from its first real beat to its last, pads inside included --
//      is at most MAX_NET_EXTENT: the gradient's stage FIFOs hold one extent while a later stage
//      waits on the net's bbox / sums, so a longer net would deadlock the device. Nets the
//      schedule would stretch further are dropped (large_extent_dropped).               [check]
// Macro-pin list (macro_pin_refs: refresh, then fold)
//  M1  Every macro pin appears exactly once; MACRO_PIN_SKIP entries are padding.        [check]
//  M2  No macro recurs within HAZARD_DISTANCE entries (the fold read-modify-write).     [check]
//  M3  FIRST marks a macro's first entry (starts its sum), LAST its last (writes the total into
//      the macro's gradient).                                                        [harness]
// Chunking (encode_chunked, required to handle designs too big for URAM)
//  C1  Every movable node is owned by exactly one chunk; a macro and its macro pins by the same one.
//  C2  Every small net, and with cfg.large_nets every large net, is homed in one chunk (majority
//      of its movable nodes); its nodes owned elsewhere are borrowed nodes there. A large net then
//      follows L1-L8 inside its home chunk's stream; drops sum into Chunked::large_dropped.
//  C3, C4  retired 2026-10-09 (#47 B3) with the DDR mailbox they described; the borrowed-copy exchange
//      (C7) replaced it.                                                  [check_chunked: C1, C2]
//  C5  Each chunk is an ordinary stream: R, S, B and M hold per chunk.   [check_chunked decodes]
//  C6  A macro pin is never borrowed. A chunk whose nets use a pin of a macro owned elsewhere holds
//      the pin as a local slot (in its macro-pin list, refreshed and folded on chip) and the macro
//      as the borrowed node: borrowed nodes are cells and macros only.  [check_chunked]
//  C7  Borrowed nodes sit in the chunk's borrowed rows, [first_borrowed_slot, first_fixed_slot): one block per
//      owner, owners ascending, each block starting on a fresh row; inside a block every bank fills
//      its rows in emission order (the owner's slot order). borrowed_order lists the borrowed nodes in that
//      order, borrowed_blocks the blocks. Every block's send and merge op lists fit BORROWED_BUFFER_ROWS.
//      Capacity is checked on num_slots, so it covers the borrowed rows.       [check_chunked]
//  C8  Shared owned nodes (borrowed by another chunk) take the lowest owned rows of their bank, so
//      every one sits below first_private_slot: the borrowed passes reload / drain only that prefix.
//      Rows only -- banks, and so beats and hazards, are unchanged.            [check_chunked]
// =================================================================================================

#include "format/pin_record.hpp"   // pl/src/format: the device contract (build with -I<repo>/pl/src). Meow.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace packer {

using namespace pinrec;

// Nets over this are masked at read time. XPlace's ignore_net_degree is 100; ours is the largest net
// the record stream carries (Mark, 2026-10-06, #46), so 97..100-pin nets never reach the netlist. Meow.
constexpr int IGNORE_NET_DEGREE = MAX_LARGE_NET_DEGREE;   // 96
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
    bool     large_nets    = true;  // emit 17..96-pin nets after the degree-16 group (default since 2026-10-02, Mark)
    int      partition_fm_passes = 1;   // encode_chunked: FM passes on the BFS cut; 0 = the bare cut (#42, Mark 2026-10-06). Meow.
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
inline bool is_large(const std::vector<Pin>& net) {
    return (int)net.size() > LANES && (int)net.size() <= MAX_LARGE_NET_DEGREE;
}

// ---------------------------------------------------------------------------------------------
// Netlist file (#45). The packer has no parser of its own: host/sw_flat's export_netlist reads a
// design once with FlatDatabase, derives this Netlist by sw_only's rules (flat_netlist.hpp), and
// writes it here; the tier-1 harnesses and the #47 models read it back, header-only. A file is a
// test/model input, not a run-time cache: a placement run builds its Netlist in memory (no caching).
// Little-endian, no padding:
//   char[8] "AIEPNL01" | u32 name_len, name
//   u32 num_nodes | u8 movable[num_nodes] | u8 is_macro[num_nodes]
//   per axis: u32 num_offsets, f32 offset_value[num_offsets]
//   u32 num_nets | u32 degree[num_nets] | per pin, net by net: i32 node, i32 offset_key[2]
// read_netlist checks every index and degree against Netlist's contract and exits on a bad file. Meow.
constexpr char NETLIST_MAGIC[8] = {'A', 'I', 'E', 'P', 'N', 'L', '0', '1'};

inline void write_netlist(const Netlist& nl, const std::string& path) {
    FILE* out = fopen(path.c_str(), "wb");
    if (!out) { fprintf(stderr, "cannot write %s\n", path.c_str()); exit(2); }
    auto put = [&](const void* data, size_t bytes) {
        if (bytes && fwrite(data, 1, bytes, out) != bytes) { fprintf(stderr, "write failed: %s\n", path.c_str()); exit(2); }
    };
    auto put_u32 = [&](size_t value) { const uint32_t v = (uint32_t)value; put(&v, 4); };
    put(NETLIST_MAGIC, 8);
    put_u32(nl.name.size()); put(nl.name.data(), nl.name.size());
    put_u32(nl.movable.size()); put(nl.movable.data(), nl.movable.size()); put(nl.is_macro.data(), nl.is_macro.size());
    for (int axis = 0; axis < 2; axis++) {
        put_u32(nl.offset_value[axis].size());
        put(nl.offset_value[axis].data(), 4 * nl.offset_value[axis].size());
    }
    put_u32(nl.nets.size());
    for (const auto& net : nl.nets) put_u32(net.size());
    for (const auto& net : nl.nets)
        for (const Pin& pin : net) { const int32_t rec[3] = {pin.node, pin.offset_key[0], pin.offset_key[1]}; put(rec, 12); }
    if (fclose(out) != 0) { fprintf(stderr, "write failed: %s\n", path.c_str()); exit(2); }
}

inline Netlist read_netlist(const std::string& path) {
    FILE* in = fopen(path.c_str(), "rb");
    if (!in) { fprintf(stderr, "cannot open netlist %s (make -C host/sw_flat netlists)\n", path.c_str()); exit(2); }
    auto bad = [&](const char* what) { fprintf(stderr, "bad netlist %s: %s\n", path.c_str(), what); exit(2); };
    auto get = [&](void* data, size_t bytes) { if (bytes && fread(data, 1, bytes, in) != bytes) bad("truncated"); };
    auto get_u32 = [&]() { uint32_t v; get(&v, 4); return (size_t)v; };
    char magic[8];
    get(magic, 8);
    if (std::memcmp(magic, NETLIST_MAGIC, 8) != 0) bad("not an AIEPNL01 file");
    Netlist nl;
    nl.name.resize(get_u32()); get(&nl.name[0], nl.name.size());
    const size_t num_nodes = get_u32();
    nl.movable.resize(num_nodes); nl.is_macro.resize(num_nodes);
    get(nl.movable.data(), num_nodes); get(nl.is_macro.data(), num_nodes);
    for (int axis = 0; axis < 2; axis++) { nl.offset_value[axis].resize(get_u32()); get(nl.offset_value[axis].data(), 4 * nl.offset_value[axis].size()); }
    std::vector<uint32_t> degree(get_u32());
    get(degree.data(), 4 * degree.size());
    nl.nets.resize(degree.size());
    for (size_t n = 0; n < degree.size(); n++) {
        if ((int)degree[n] < MIN_NET_DEGREE || (int)degree[n] > IGNORE_NET_DEGREE) bad("net degree outside 2..IGNORE_NET_DEGREE");
        nl.nets[n].resize(degree[n]);
        for (Pin& pin : nl.nets[n]) {
            int32_t rec[3];
            get(rec, 12);
            if (rec[0] < 0 || (size_t)rec[0] >= num_nodes) bad("pin node out of range");
            for (int axis = 0; axis < 2; axis++)
                if (rec[1 + axis] < 0 || (size_t)rec[1 + axis] >= nl.offset_value[axis].size()) bad("offset key out of range");
            pin = {rec[0], {rec[1], rec[2]}};
        }
    }
    if (fgetc(in) != EOF) bad("trailing bytes");
    fclose(in);
    return nl;
}

// ---------------------------------------------------------------------------------------------
// Encoding.

struct Beat {
    int degree;
    std::vector<int> nets;
    // A large-net beat (degree > LANES) carries `pins` (indices into its net), one beat of a `span`-beat net. Meow.
    int  span        = 1;
    bool last_of_net = false;
    std::vector<int> pins;
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
    std::vector<int>      macro_order;      // device order: index into macro_pins, or -1 (SKIP)
    std::vector<std::vector<int>> unique_nodes;       // per net: distinct work nodes

    std::vector<int>  in_scope_nets;
    std::vector<int>  shared_bank;          // per work node: its bank in every chunk (resident shared node), NO_BANK = free; empty = none
    std::vector<int>  bank;                 // per work node, NO_BANK if it has no slot
    long              colliding_nets = 0;   // nets whose own nodes share a bank: not encodable
    std::vector<Beat> beats;
    long              ideal_beats = 0;      // sum over degrees of ceil(nets / nets_per_beat)
    std::vector<int>  issue;                // beat index per stream position, BUBBLE = all-EMPTY beat
    std::vector<int>  issue_degree;         // degree group of each stream position (bubbles included)
    std::vector<int>  issue_span;           // span group of each large-net position (pads included); 0 if small
    long              large_pads = 0;       // pad beats in the large-net section (contract L7)
    std::vector<int>  large_nets;           // encoded large nets (cfg.large_nets), in stream order
    long              large_dropped = 0;    // large nets not encodable: span > MAX_SPAN, or a node with > LANES pins
    long              large_extent_dropped = 0;   // of those, extent > MAX_NET_EXTENT (contract L8)
    long              large_extra_span = 0; // large nets whose span exceeds ceil(degree / LANES)

    std::vector<long> node_slot;            // per work node, -1 if none
    std::vector<int>  borrowed_from;        // per work node: its owner chunk if borrowed (borrowed rows, C7), else -1; empty = none
    std::vector<char> shared;               // per work node: owned here AND borrowed by another chunk (C8); empty = none
    long              first_private_slot = 0;   // owned slots below hold every shared node (C8); 0 unchunked
    long              first_borrowed_slot = 0;  // owned movable slots below, borrowed rows from here (C7)
    long              first_fixed_slot = 0;
    long              num_slots = 0;        // multiple of BANKS (and so of LANES)
    long              max_slot = 0;
    int               offset_bits = 0;
    bool              fits = false;         // max_slot below the EMPTY node field for offset_bits
    long              uram_grad = 0, uram_pos = 0;

    std::vector<uint32_t> records[2];       // per axis, LANES per stream position
    std::vector<int>  degree_group_end;     // [NET_DEGREES_PROCESSED] cumulative positions per degree
    std::vector<int>  span_group_end;       // [SPAN_GROUPS] cumulative positions per span, continuing degree_group_end
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
// in-scope net take the least-loaded bank.
// With cfg.large_nets, a large net packs into its minimum ceil(degree/LANES) beats only if no bank
// holds more of its nodes than that, since a beat takes one node per bank. So each large net is a
// soft per-bank cap: below every small-net constraint, above load balance.
// Nodes with a shared_bank take it first and never move; the rest colour around them. Meow.
inline void assign_banks(Encoded& enc, const Config& cfg) {
    const Netlist& work = enc.work;
    const int num_nodes = (int)work.movable.size();
    std::vector<std::vector<int>> node_nets(num_nodes);
    for (int net : enc.in_scope_nets)
        for (int node : enc.unique_nodes[net]) node_nets[node].push_back(net);

    std::vector<std::vector<int>> node_large(num_nodes);     // large-net index per node
    std::vector<int> large_cap;
    std::vector<std::array<int, BANKS>> large_load;          // per large net: its nodes per bank
    if (cfg.large_nets)
        for (int net = 0; net < (int)work.nets.size(); net++) {
            if (!is_large(work.nets[net])) continue;
            for (int node : enc.unique_nodes[net]) node_large[node].push_back((int)large_cap.size());
            large_cap.push_back(((int)work.nets[net].size() + LANES - 1) / LANES);
            large_load.push_back({});
        }
    auto over_cap_banks = [&](int node) {   // banks where this node would push a large net over its cap
        uint64_t full = 0;
        for (int large : node_large[node])
            for (int k = 0; k < BANKS; k++)
                if (large_load[large][k] >= large_cap[large]) full |= 1ull << k;
        return full;
    };
    auto set_bank = [&](int node, int bank) {
        for (int large : node_large[node]) {
            if (enc.bank[node] != NO_BANK) large_load[large][enc.bank[node]]--;
            large_load[large][bank]++;
        }
        enc.bank[node] = bank;
    };

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
    auto has_shared_bank = [&](int node) { return !enc.shared_bank.empty() && enc.shared_bank[node] != NO_BANK; };
    for (int node : order)
        if (has_shared_bank(node)) { set_bank(node, enc.shared_bank[node]); bank_load[slot_class(enc.kind[node])][enc.bank[node]]++; }
    for (int node : order) {
        if (has_shared_bank(node)) continue;
        auto& load = bank_load[slot_class(enc.kind[node])];
        uint64_t forbidden = 0;
        for (int net : node_nets[node])
            for (int other : enc.unique_nodes[net])
                if (enc.bank[other] != NO_BANK) forbidden |= 1ull << enc.bank[other];
        const uint64_t full = over_cap_banks(node);
        int best = -1;
        for (int k = 0; k < BANKS; k++)
            if (!(forbidden >> k & 1) && !(full >> k & 1) && (best < 0 || load[k] < load[best])) best = k;
        if (best < 0)
            for (int k = 0; k < BANKS; k++)
                if (!(forbidden >> k & 1) && (best < 0 || load[k] < load[best])) best = k;
        if (best < 0) best = (int)(std::min_element(load.begin(), load.end()) - load.begin());
        set_bank(node, best);
        load[best]++;
    }

    // Repair cost per bank: small-net collisions first, then large nets pushed over their cap. Meow.
    std::vector<long> collisions(BANKS);
    const long SMALL_COLLISION_COST = 1L << 20;
    for (int pass = 0; pass < cfg.repair_passes; pass++) {
        long moved = 0;
        for (int node : order) {
            if (has_shared_bank(node) || (node_nets[node].empty() && node_large[node].empty())) continue;
            auto& load = bank_load[slot_class(enc.kind[node])];
            const int current = enc.bank[node];
            std::fill(collisions.begin(), collisions.end(), 0);
            for (int net : node_nets[node])
                for (int other : enc.unique_nodes[net])
                    if (other != node) collisions[enc.bank[other]] += SMALL_COLLISION_COST;
            for (int large : node_large[node])
                for (int k = 0; k < BANKS; k++)
                    collisions[k] += large_load[large][k] - (k == current) >= large_cap[large];
            if (collisions[current] == 0) continue;
            int best = current;
            for (int k = 0; k < BANKS; k++)
                if (collisions[k] < collisions[best] ||
                    (collisions[k] == collisions[best] && load[k] < load[best])) best = k;
            if (best == current) continue;
            load[current]--; load[best]++;
            set_bank(node, best);
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
            enc.issue_span.push_back(0);
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

// Step 3b (cfg.large_nets): each large net becomes `span` consecutive beats, appended after the
// small-net stream in ascending span groups. A node's pins on the net form one lane run, split into
// runs of at most LANES (MMS has cells and fixed pins with up to ~36 pins on one 2-3-node net); runs
// of one node share a bank, so they land in different beats. Opens the minimum ceil(degree/LANES)
// beats, then places runs, busiest bank first, into the least-filled beat with lanes left and the
// run's bank free; a new beat only when none fits.
// Then the hazard schedule (contract L6), continuing the small-net stream's last-update times: per
// span group, list-schedule whole nets (the first within `window` that needs no pad, else the one
// needing fewest), and within a net emit whichever remaining beat is ready, a PAD when none is. Meow.
inline void pack_large_nets(Encoded& enc, const Config& cfg) {
    if (!cfg.large_nets) return;
    struct Fill { int lanes = 0; uint64_t banks = 0; std::vector<int> pins; std::vector<int> nodes; };
    std::vector<std::vector<std::pair<int, std::vector<Fill>>>> by_span(MAX_SPAN + 1);
    for (int net = 0; net < (int)enc.work.nets.size(); net++) {
        const std::vector<Pin>& net_pins = enc.work.nets[net];
        if (!is_large(net_pins)) continue;
        std::map<int, std::vector<int>> node_pins;
        for (int p = 0; p < (int)net_pins.size(); p++) node_pins[net_pins[p].node].push_back(p);
        std::vector<std::pair<int, std::vector<int>>> runs;   // (bank, pin indices)
        for (auto& [node, pins] : node_pins)
            for (size_t first = 0; first < pins.size(); first += LANES)
                runs.emplace_back(enc.bank[node], std::vector<int>(pins.begin() + first,
                                                                   pins.begin() + std::min(pins.size(), first + LANES)));
        std::array<int, BANKS> bank_runs{};
        for (const auto& run : runs) bank_runs[run.first]++;
        if (*std::max_element(bank_runs.begin(), bank_runs.end()) > MAX_SPAN) { enc.large_dropped++; continue; }
        std::stable_sort(runs.begin(), runs.end(), [&](const auto& a, const auto& b) {
            return std::make_pair(bank_runs[a.first], a.second.size()) > std::make_pair(bank_runs[b.first], b.second.size()); });
        const int min_span = ((int)net_pins.size() + LANES - 1) / LANES;
        std::vector<Fill> beats(min_span);
        for (const auto& [bank, pins] : runs) {
            const uint64_t bank_bit = 1ull << bank;
            auto home = beats.end();
            for (auto f = beats.begin(); f != beats.end(); ++f)
                if (f->lanes + (int)pins.size() <= LANES && !(f->banks & bank_bit) &&
                    (home == beats.end() || f->lanes < home->lanes)) home = f;
            if (home == beats.end()) home = beats.insert(beats.end(), Fill());
            home->lanes += (int)pins.size();
            home->banks |= bank_bit;
            home->pins.insert(home->pins.end(), pins.begin(), pins.end());
            home->nodes.push_back(net_pins[pins[0]].node);
        }
        const int span = (int)beats.size();
        if (span > MAX_SPAN) { enc.large_dropped++; continue; }
        enc.large_extra_span += span > min_span;
        enc.ideal_beats += min_span;
        by_span[span].emplace_back(net, std::move(beats));
    }

    std::vector<long> last_update(enc.work.movable.size(), -(long)cfg.hazard);
    for (size_t position = 0; position < enc.issue.size(); position++)
        if (enc.issue[position] != BUBBLE)
            for (int net : enc.beats[enc.issue[position]].nets)
                for (int node : enc.unique_nodes[net]) if (enc.work.movable[node]) last_update[node] = (long)position;
    auto ready = [&](const Fill& f, long position) {
        for (int node : f.nodes)
            if (enc.work.movable[node] && position - last_update[node] < cfg.hazard) return false;
        return true;
    };
    // Emit (or, with commit=false, only count the pads of) one net starting at `position`; `extent`
    // gets its first-to-last real beat distance (contract L8).
    auto place_net = [&](int net, std::vector<Fill>& beats, int span, bool commit, long& extent) {
        std::vector<long> saved;
        if (!commit) for (const Fill& f : beats) for (int node : f.nodes) saved.push_back(last_update[node]);
        std::vector<char> done(beats.size(), 0);
        long position = (long)enc.issue.size(), pads = 0, first_real = -1;
        for (size_t emitted = 0; emitted < beats.size(); position++) {
            int pick = -1;
            for (size_t b = 0; b < beats.size() && pick < 0; b++) if (!done[b] && ready(beats[b], position)) pick = (int)b;
            if (commit) {
                enc.issue_degree.push_back((int)enc.work.nets[net].size());
                enc.issue_span.push_back(span);
            }
            if (pick < 0) {
                pads++;
                if (commit) { enc.issue.push_back(BUBBLE); enc.large_pads++; }
                continue;
            }
            done[pick] = 1;
            emitted++;
            if (first_real < 0) first_real = position;
            extent = position - first_real + 1;
            for (int node : beats[pick].nodes) if (enc.work.movable[node]) last_update[node] = position;
            if (commit) {
                Beat beat;
                beat.degree      = (int)enc.work.nets[net].size();
                beat.nets        = {net};
                beat.span        = span;
                beat.last_of_net = emitted == beats.size();
                beat.pins        = std::move(beats[pick].pins);
                enc.issue.push_back((int)enc.beats.size());
                enc.beats.push_back(std::move(beat));
            }
        }
        if (!commit) {
            size_t i = 0;
            for (const Fill& f : beats) for (int node : f.nodes) last_update[node] = saved[i++];
        }
        return pads;
    };
    for (int span = 2; span <= MAX_SPAN; span++) {
        auto& group = by_span[span];
        std::deque<int> pending;
        for (size_t i = 0; i < group.size(); i++) pending.push_back((int)i);
        while (!pending.empty()) {
            int pick = 0;
            long fewest = -1;
            long extent = 0, pick_extent = 0;
            for (int scan = 0; scan < (int)pending.size() && scan < cfg.window; scan++) {
                const long pads = place_net(group[pending[scan]].first, group[pending[scan]].second, span, false, extent);
                if (fewest < 0 || pads < fewest) { fewest = pads; pick = scan; pick_extent = extent; }
                if (pads == 0) break;
            }
            auto& [net, beats] = group[pending[pick]];
            pending.erase(pending.begin() + pick);
            if (pick_extent > MAX_NET_EXTENT) { enc.large_dropped++; enc.large_extent_dropped++; continue; }
            enc.large_nets.push_back(net);
            place_net(net, beats, span, true, extent);
        }
    }
}

// Step 4: node_slot = row * BANKS + bank. Owned movable rows first in every bank, then the
// borrowed rows (C7; none unchunked), then fixed rows, so `node_slot >= first_fixed_slot` is
// the device's "read position, skip gradient write" test. The borrowed rows are one block per owner, owners
// ascending, each on fresh rows; a borrowed node's row inside its block is provisional here:
// order_borrowed_blocks puts each block in emission order once every chunk is encoded. Meow.
inline void assign_node_slots(Encoded& enc) {
    const auto& kind = enc.kind;
    enum { FIXED_ROWS, OWNED_ROWS, BORROWED_ROWS };
    auto is_borrowed = [&](size_t node) { return !enc.borrowed_from.empty() && enc.borrowed_from[node] >= 0; };
    auto row_class = [&](size_t node) {
        if (slot_class(kind[node]) == 0) return (int)FIXED_ROWS;
        return is_borrowed(node) ? (int)BORROWED_ROWS : (int)OWNED_ROWS;
    };
    std::vector<long> rows[3] = {std::vector<long>(BANKS, 0), std::vector<long>(BANKS, 0), std::vector<long>(BANKS, 0)};
    std::map<int, std::vector<long>> block_bank_rows;   // owner -> rows per bank of its block
    auto shared = [&](size_t node) { return !enc.shared.empty() && enc.shared[node]; };
    std::vector<long> shared_rows(BANKS, 0);
    for (size_t node = 0; node < kind.size(); node++) {
        if (enc.bank[node] == NO_BANK) continue;
        rows[row_class(node)][enc.bank[node]]++;
        if (shared(node)) shared_rows[enc.bank[node]]++;
        if (row_class(node) == BORROWED_ROWS) {
            auto& counts = block_bank_rows[enc.borrowed_from[node]];
            counts.resize(BANKS, 0);
            counts[enc.bank[node]]++;
        }
    }
    const long owned_rows = *std::max_element(rows[OWNED_ROWS].begin(), rows[OWNED_ROWS].end());
    long borrowed_rows = 0;
    std::map<int, long> block_start;   // owner -> its block's first row
    for (const auto& [owner, counts] : block_bank_rows) {
        block_start[owner] = owned_rows + borrowed_rows;
        borrowed_rows += *std::max_element(counts.begin(), counts.end());
    }
    const long fixed_rows = *std::max_element(rows[FIXED_ROWS].begin(), rows[FIXED_ROWS].end());
    const long movable_rows = owned_rows + borrowed_rows;
    enc.first_private_slot = *std::max_element(shared_rows.begin(), shared_rows.end()) * BANKS;
    enc.first_borrowed_slot = owned_rows * BANKS;
    enc.first_fixed_slot = movable_rows * BANKS;
    enc.num_slots        = (movable_rows + fixed_rows) * BANKS;

    // C8: a bank's shared nodes take its rows from 0, its private owned nodes follow them. Meow.
    std::vector<long> next_row[2] = {std::vector<long>(BANKS, movable_rows), shared_rows};
    std::vector<long> next_shared_row(BANKS, 0);
    std::map<int, std::vector<long>> block_next_row;
    for (const auto& [owner, start] : block_start) block_next_row[owner].assign(BANKS, start);
    enc.node_slot.assign(kind.size(), -1);
    for (size_t node = 0; node < kind.size(); node++) {
        const int bank = enc.bank[node];
        if (bank == NO_BANK) continue;
        const long row = is_borrowed(node) ? block_next_row[enc.borrowed_from[node]][bank]++
                       : shared(node) ? next_shared_row[bank]++
                                      : next_row[row_class(node)][bank]++;
        enc.node_slot[node] = row * BANKS + bank;
        enc.max_slot = std::max(enc.max_slot, enc.node_slot[node]);
    }
    const long words_per_uram = (long)URAM_WORDS * FLOATS_PER_WORD;
    enc.uram_grad = BANKS * ((movable_rows + words_per_uram - 1) / words_per_uram);
    enc.uram_pos  = BANKS * ((movable_rows + fixed_rows + words_per_uram - 1) / words_per_uram);
}

// Step 4b: order the macro-pin list for the device's read-add-write fold: no macro within
// `hazard` entries of itself. Largest-remaining-first with a cooldown (the task-scheduler
// greedy), a SKIP entry only when every macro with pins left is still cooling. Meow.
inline void schedule_macro_pins(Encoded& enc, const Config& cfg) {
    std::map<int, std::vector<int>> by_macro;
    for (size_t i = 0; i < enc.macro_pins.size(); i++) by_macro[enc.macro_pins[i].macro_node].push_back((int)i);
    std::vector<std::vector<int>> pins;
    for (auto& entry : by_macro) pins.push_back(entry.second);
    std::vector<size_t> next(pins.size(), 0);
    std::vector<long> ready(pins.size(), 0);
    long position = 0;
    size_t remaining = enc.macro_pins.size();
    enc.macro_order.clear();
    while (remaining) {
        int best = -1;
        for (size_t m = 0; m < pins.size(); m++)
            if (next[m] < pins[m].size() && ready[m] <= position &&
                (best < 0 || pins[m].size() - next[m] > pins[best].size() - next[best])) best = (int)m;
        if (best < 0) enc.macro_order.push_back(-1);
        else { enc.macro_order.push_back(pins[best][next[best]++]); ready[best] = position + cfg.hazard; remaining--; }
        position++;
    }
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
        if (beat.span > 1) {   // large-net beat: its nodes' pins from lane 0; the net is reported on its last beat
            if (beat.last_of_net) enc.net_at[position * MAX_NETS_PER_BEAT] = beat.nets[0];
            std::vector<Pin> pins;
            for (int p : beat.pins) pins.push_back(enc.work.nets[beat.nets[0]][p]);
            std::stable_sort(pins.begin(), pins.end(),
                             [&](const Pin& a, const Pin& b) { return enc.node_slot[a.node] < enc.node_slot[b.node]; });
            for (size_t p = 0; p < pins.size(); p++)
                for (int axis = 0; axis < 2; axis++)
                    enc.records[axis][position * LANES + p] =
                        make_record((uint32_t)enc.node_slot[pins[p].node], (uint32_t)pins[p].offset_key[axis], enc.offset_bits);
            continue;
        }
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
    enc.degree_group_end.assign(NET_DEGREES_PROCESSED, 0);
    for (int degree : enc.issue_degree)
        for (int i = degree - MIN_NET_DEGREE; i < NET_DEGREES_PROCESSED; i++) enc.degree_group_end[i]++;
    enc.span_group_end.assign(SPAN_GROUPS, enc.degree_group_end.back());
    for (int span : enc.issue_span)
        if (span > 1)
            for (int i = span - 2; i < SPAN_GROUPS; i++) enc.span_group_end[i]++;
}

// Steps 1-5 on an Encoded whose work netlist, kinds, pin-node maps, unique_nodes and in_scope_nets
// are already set -- by resolve_pin_nodes for a whole design, or by encode_chunked for one chunk. Meow.
inline void encode_resolved(Encoded& enc, const Config& cfg) {
    std::mt19937 rng(cfg.seed);
    assign_banks(enc, cfg);
    pack_beats(enc, cfg, rng);
    schedule_beats(enc, cfg);
    pack_large_nets(enc, cfg);
    assign_node_slots(enc);
    schedule_macro_pins(enc, cfg);
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
    std::map<int, int> first, last;   // macro_node -> position in macro_order
    for (size_t e = 0; e < enc.macro_order.size(); e++) {
        if (enc.macro_order[e] < 0) continue;
        const int macro = enc.macro_pins[enc.macro_order[e]].macro_node;
        if (!first.count(macro)) first[macro] = (int)e;
        last[macro] = (int)e;
    }
    std::vector<MacroPinRef> refs;
    for (size_t e = 0; e < enc.macro_order.size(); e++) {
        if (enc.macro_order[e] < 0) { refs.push_back({MACRO_PIN_SKIP, MACRO_PIN_SKIP, 0.0f, 0}); continue; }
        const MacroPin& mp = enc.macro_pins[enc.macro_order[e]];
        const int32_t flags = (first[mp.macro_node] == (int)e ? MACRO_PIN_FIRST : 0) | (last[mp.macro_node] == (int)e ? MACRO_PIN_LAST : 0);
        refs.push_back({(int32_t)enc.node_slot[mp.pin_node], (int32_t)enc.node_slot[mp.macro_node], mp.offset[axis], flags});
    }
    return refs;
}

// ---------------------------------------------------------------------------------------------
// Independent checker: decodes the record streams using only what the device sees (records,
// degree_group_end, offset_bits, first_fixed_slot, offset tables) plus the slot->node inverse, and
// requires the decoded geometry -- (parsed node, total offset) per pin -- to equal the in-scope
// nets of the PARSED netlist, so a wrong pin-node rewrite is caught too. Every beat must also
// satisfy the bank, hazard and adjacency rules. Meow.

using GeomPin = std::tuple<int, float, float>;

// The in-scope nets plus `large_nets` (indices into nl.nets: the encoder reports which large nets it
// kept; their geometry is still checked here against the parsed netlist). Meow.
inline std::vector<std::vector<GeomPin>> in_scope_geometry(const Netlist& nl, const std::vector<int>& large_nets = {}) {
    std::vector<char> keep(nl.nets.size());
    for (size_t n = 0; n < nl.nets.size(); n++) keep[n] = in_scope(nl.nets[n]);
    for (int n : large_nets) keep[n] = 1;
    std::vector<std::vector<GeomPin>> nets;
    for (size_t n = 0; n < nl.nets.size(); n++) {
        if (!keep[n]) continue;
        const auto& net = nl.nets[n];
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
    const long small_positions = enc.degree_group_end.back();
    if (enc.span_group_end.back() != num_positions) fail("span_group_end does not end the stream", num_positions);
    for (long position = 0; position < small_positions; position++) {
        int degree = MIN_NET_DEGREE;   // resolve_beat's rule, from cumulative counts
        for (int group_end : enc.degree_group_end) if (position >= group_end) degree++;
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
    // Large-net section, decoded by the device's rules: a pad (lane 0 EMPTY) must be all EMPTY and
    // is skipped; the span comes from span_group_end at the net's first real beat, and the net
    // ends after that many real beats. In a real beat EMPTY lanes only trail, and a node's pins are
    // adjacent with banks distinct (a node may recur in a later beat of the net). The hazard rule
    // continues from the small-net section's last_update. Meow.
    long position = small_positions;
    auto skip_pads = [&]() {
        while (position < num_positions && enc.records[0][position * LANES] == EMPTY_RECORD) {
            for (int lane = 0; lane < LANES; lane++)
                if (enc.records[0][position * LANES + lane] != EMPTY_RECORD ||
                    enc.records[1][position * LANES + lane] != EMPTY_RECORD) fail("pad beat not all EMPTY", position);
            position++;
        }
    };
    for (skip_pads(); position < num_positions; skip_pads()) {
        int span = 2;
        for (int group_end : enc.span_group_end) if (position >= group_end) span++;
        std::vector<GeomPin> net;
        const long first_real = position;
        for (int b = 0; b < span; b++, position++) {
            if (b > 0) skip_pads();
            if (position >= num_positions) { fail("large net cut short by the stream end", position); break; }
            if (position - first_real + 1 > MAX_NET_EXTENT) fail("large net extent > MAX_NET_EXTENT", position);
            std::vector<uint32_t> net_slots;
            uint64_t banks_used = 0;
            bool trailing = false;
            for (int lane = 0; lane < LANES; lane++) {
                const uint32_t rx = enc.records[0][position * LANES + lane], ry = enc.records[1][position * LANES + lane];
                if (record_node_slot(rx, ob) != record_node_slot(ry, ob)) fail("x and y node_slot differ", position);
                if (rx == EMPTY_RECORD) { trailing = true; continue; }
                if (trailing) { fail("pin after an EMPTY lane in a large-net beat", position); continue; }
                const uint32_t slot = record_node_slot(rx, ob);
                auto it = slot_to_node.find(slot);
                if (it == slot_to_node.end()) { fail("node_slot maps to no node", position); continue; }
                if (net_slots.empty() || net_slots.back() != slot) {
                    if (std::count(net_slots.begin(), net_slots.end(), slot)) fail("repeated node not adjacent", position);
                    if (banks_used >> (slot % BANKS) & 1) fail("bank hit twice in one beat", position);
                    banks_used |= 1ull << (slot % BANKS);
                    net_slots.push_back(slot);
                    if (slot < enc.first_fixed_slot) {
                        auto last = last_update.find(slot);
                        if (last != last_update.end() && position - last->second < cfg.hazard) fail("RAW hazard (large net)", position);
                        last_update[slot] = position;
                    }
                }
                const int node = it->second;
                const uint32_t idx_x = record_offset_idx(rx, ob), idx_y = record_offset_idx(ry, ob);
                if (idx_x >= enc.offset_table[0].size() || idx_y >= enc.offset_table[1].size()) {
                    fail("offset_idx past table end", position); continue;
                }
                net.emplace_back(enc.base_node[node], enc.pinned_offset[node][0] + enc.offset_table[0][idx_x],
                                                      enc.pinned_offset[node][1] + enc.offset_table[1][idx_y]);
            }
        }
        std::sort(net.begin(), net.end());
        decoded.push_back(std::move(net));
    }
    for (const auto& entry : slot_to_node)
        if ((entry.first < enc.first_fixed_slot) != (bool)enc.work.movable[entry.second]) fail("movable/fixed slot range wrong", entry.first);
    for (const MacroPin& mp : enc.macro_pins)
        if (enc.base_node[mp.pin_node] != enc.base_node[mp.macro_node]) fail("macro pin list names the wrong macro", mp.pin_node);
    std::vector<int> seen(enc.macro_pins.size(), 0);
    std::unordered_map<int, long> last_fold;
    for (size_t e = 0; e < enc.macro_order.size(); e++) {
        const int i = enc.macro_order[e];
        if (i < 0) continue;
        if (i >= (int)seen.size()) { fail("macro order names no macro pin", (long)e); continue; }
        seen[i]++;
        auto it = last_fold.find(enc.macro_pins[i].macro_node);
        if (it != last_fold.end() && (long)e - it->second < cfg.hazard) fail("macro fold RAW hazard", (long)e);
        last_fold[enc.macro_pins[i].macro_node] = (long)e;
    }
    for (size_t i = 0; i < seen.size(); i++) if (seen[i] != 1) fail("macro pin not folded exactly once", (long)i);
}

inline int check(const Netlist& nl, const Encoded& enc, const Config& cfg) {
    Failures fail;
    std::vector<std::vector<GeomPin>> decoded;
    decode_stream(enc, cfg, fail, decoded);
    if (!enc.fits) return fail.count;
    for (size_t node = 0; node < nl.movable.size(); node++)
        if (nl.movable[node] && enc.node_slot[node] < 0) fail("movable node without a slot", (long)node);
    std::sort(decoded.begin(), decoded.end());
    if (decoded != in_scope_geometry(nl, enc.large_nets)) fail("decoded netlist differs from input", (long)decoded.size());
    return fail.count;
}

// ---------------------------------------------------------------------------------------------
// Chunking: when the slots exceed on-chip capacity, split the design into K chunks that each fit.
//
// Every movable node is OWNED by one chunk (a macro and its macro pins share one). Every in-scope
// net (plus every large net, with cfg.large_nets) is HOMED in the chunk that owns most of its
// movable nodes; the chunk's stream carries its
// homed nets. A homed net's node owned elsewhere becomes a BORROWED node in the home chunk (a local
// copy of another chunk's node); fixed pin nodes are simply copied into every chunk that uses them
// (their position never changes, so they are not borrowed).
// Each chunk is then an ordinary Encoded stream over local slots, so node_slot bits shrink too.
//
// Borrowed nodes' values move through the borrowers' BORROWED rows (C7): the owner writes a block's positions
// into the borrower's position image and later adds what the borrower drained into the same block of
// the gradient image into its own (pin_record.hpp BorrowedBlockDesc). Every DDR access is sequential; the
// random side is always on chip. Meow.

struct Chunk {
    Encoded enc;                             // local: enc.work node ids are chunk-local
    std::vector<int>  net_global;            // local net -> global net (= parsed net index)
    std::vector<int>  work_node;             // local node -> global work node
    std::vector<char> borrowed;              // per local node: borrowed, i.e. owned by another chunk
    std::vector<int>  borrowed_order;        // borrowed locals in emission order: owner ascending, then owner slot (C7)
    struct BorrowedBlock { int owner; long first, count, start_row, rows; };   // borrowed_order[first, first+count)
    std::vector<BorrowedBlock> borrowed_blocks;  // one per owner with nodes borrowed here, owners ascending (C7)
};

struct Chunked {
    Encoded global;                          // resolved work netlist (not encoded)
    long capacity = 0;
    int  num_chunks = 0;
    std::vector<int> owner;                  // per global work node, -1 for fixed
    std::vector<int> home;                   // per global net, -1 if out of scope
    std::vector<Chunk> chunks;
    long num_borrowed = 0;
    long large_dropped = 0;                  // summed over chunks (see Encoded::large_dropped)
    bool fits = false;
    long fm_target = 0;                      // encode_chunked: FM's slot target for this partition, 0 = the bare cut
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

// The baseline partition: K equal contiguous runs of the locality order. A run boundary can fall
// between a macro and its pins; the pins follow the macro here, as build_chunks_from_owner would,
// because FM moves a macro with its pins as one unit and needs the unit whole from the start. Meow.
inline std::vector<int> order_owner(const Encoded& g, const std::vector<int>& order, int num_chunks) {
    std::vector<int> owner(g.kind.size(), -1);
    const long per_chunk = ((long)order.size() + num_chunks - 1) / num_chunks;
    for (size_t i = 0; i < order.size(); i++) owner[order[i]] = std::min<long>((long)i / per_chunk, num_chunks - 1);
    for (const MacroPin& mp : g.macro_pins) owner[mp.pin_node] = owner[mp.macro_node];
    return owner;
}

// Builds every chunk from a given owner per movable work node (-1 for fixed), any partitioner's
// (#42). A macro's pins are forced to the macro's chunk. False if a chunk does not fit. Meow.
// C7: a borrower's borrowed rows hold one block per owner, owners ascending, each on fresh rows, and inside
// a block every bank fills its rows in EMISSION order -- the owner's slot order, the order its
// update streams them -- so a block can be assembled row by row. Owner slots exist only once every
// chunk is encoded, so this runs last and re-encodes the records of each chunk it reorders (the
// permutation stays inside each bank's rows of one block: banks, beats and hazards are unchanged). Meow.
inline void order_borrowed_blocks(Chunked& ch) {
    std::vector<long> owner_slot(ch.global.kind.size(), -1);
    for (int j = 0; j < ch.num_chunks; j++) {
        const Chunk& c = ch.chunks[j];
        for (size_t l = 0; l < c.work_node.size(); l++)
            if (!c.borrowed[l] && ch.owner[c.work_node[l]] == j) owner_slot[c.work_node[l]] = c.enc.node_slot[l];
    }
    for (Chunk& c : ch.chunks) {
        c.borrowed_order.clear();
        for (size_t l = 0; l < c.work_node.size(); l++) if (c.borrowed[l]) c.borrowed_order.push_back((int)l);
        if (c.borrowed_order.empty()) continue;
        std::stable_sort(c.borrowed_order.begin(), c.borrowed_order.end(), [&](int a, int b) {
            const int na = c.work_node[a], nb = c.work_node[b];
            return ch.owner[na] != ch.owner[nb] ? ch.owner[na] < ch.owner[nb] : owner_slot[na] < owner_slot[nb]; });
        c.borrowed_blocks.clear();
        long start_row = c.enc.first_borrowed_slot / BANKS;
        for (size_t first = 0; first < c.borrowed_order.size();) {
            const int owner = ch.owner[c.work_node[c.borrowed_order[first]]];
            size_t end = first;
            std::vector<long> next_row(BANKS, start_row);
            for (; end < c.borrowed_order.size() && ch.owner[c.work_node[c.borrowed_order[end]]] == owner; end++) {
                const int l = c.borrowed_order[end];
                c.enc.node_slot[l] = next_row[c.enc.bank[l]]++ * BANKS + c.enc.bank[l];
            }
            const long rows = *std::max_element(next_row.begin(), next_row.end()) - start_row;
            c.borrowed_blocks.push_back({owner, (long)first, (long)(end - first), start_row, rows});
            start_row += rows;
            first = end;
        }
        encode_records(c.enc);
    }
}

inline bool build_chunks_from_owner(Chunked& ch, std::vector<int> owner, int num_chunks, const Config& cfg) {
    const Encoded& g = ch.global;
    const int num_nodes = (int)g.kind.size();
    ch.num_chunks = num_chunks;
    ch.owner = std::move(owner);
    for (const MacroPin& mp : g.macro_pins) ch.owner[mp.pin_node] = ch.owner[mp.macro_node];   // same unit

    std::vector<int> homed_nets = g.in_scope_nets;   // small nets first, so a chunk's local net ids follow suit
    if (cfg.large_nets)
        for (int net = 0; net < (int)g.work.nets.size(); net++) if (is_large(g.work.nets[net])) homed_nets.push_back(net);
    ch.home.assign(g.work.nets.size(), -1);
    std::vector<long> votes(num_chunks);
    for (int net : homed_nets) {
        std::fill(votes.begin(), votes.end(), 0);
        for (int node : g.unique_nodes[net]) if (ch.owner[node] >= 0) votes[ch.owner[node]]++;
        ch.home[net] = (int)(std::max_element(votes.begin(), votes.end()) - votes.begin());
    }

    // C8: a node is shared when a net homed in another chunk uses it, or (C6) uses one of its macro's
    // pins -- exactly the nodes the loop below makes borrowed somewhere. Meow.
    std::vector<char> shared(num_nodes, 0);
    for (int net : homed_nets)
        for (int node : g.unique_nodes[net]) {
            const int unit = g.kind[node] == MACRO_PIN ? g.base_node[node] : node;
            if (g.work.movable[unit] && ch.owner[unit] >= 0 && ch.owner[unit] != ch.home[net]) shared[unit] = 1;
        }

    ch.chunks.assign(num_chunks, Chunk());
    ch.num_borrowed = 0;
    ch.large_dropped = 0;
    for (int k = 0; k < num_chunks; k++) {
        Chunk& c = ch.chunks[k];
        std::vector<int> local_of(num_nodes, -1);
        auto add_node = [&](int node, bool is_borrowed) {
            local_of[node] = (int)c.work_node.size();
            c.work_node.push_back(node);
            c.borrowed.push_back(is_borrowed);
        };
        for (int node = 0; node < num_nodes; node++) if (ch.owner[node] == k) add_node(node, false);
        std::vector<int> nets;
        for (int net : homed_nets) if (ch.home[net] == k) nets.push_back(net);
        // Rule C6: a pin of a macro owned elsewhere is a local slot, refreshed on chip from its macro,
        // and the MACRO is the borrowed node -- so borrowed nodes are only cells and macros. Fixed pins are
        // copies, not borrowed. Meow.
        for (int net : nets)
            for (int node : g.unique_nodes[net]) {
                if (local_of[node] >= 0) continue;
                if (g.kind[node] == MACRO_PIN) {
                    if (local_of[g.base_node[node]] < 0) add_node(g.base_node[node], true);
                    add_node(node, false);
                } else {
                    add_node(node, g.work.movable[node]);
                }
            }

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
            if (in_scope(local_net)) e.in_scope_nets.push_back((int)e.work.nets.size());   // pack_large_nets finds the rest by degree
            c.net_global.push_back(net);
            e.work.nets.push_back(std::move(local_net));
            e.unique_nodes.push_back(std::move(local_unique));
        }
        for (const MacroPin& mp : g.macro_pins)
            if (local_of[mp.pin_node] >= 0)   // owned macros: all pins; borrowed macros: the pins its nets use (C6). Meow.
                e.macro_pins.push_back({local_of[mp.pin_node], local_of[mp.macro_node], {mp.offset[0], mp.offset[1]}});
        if (std::count(c.borrowed.begin(), c.borrowed.end(), 1)) {   // C7
            e.borrowed_from.assign(c.work_node.size(), -1);
            for (size_t l = 0; l < c.work_node.size(); l++) if (c.borrowed[l]) e.borrowed_from[l] = ch.owner[c.work_node[l]];
        }
        e.shared.assign(c.work_node.size(), 0);
        for (size_t l = 0; l < c.work_node.size(); l++) e.shared[l] = !c.borrowed[l] && shared[c.work_node[l]];
        encode_resolved(e, cfg);
        if (!e.fits || e.num_slots > ch.capacity) return false;
        for (char is_borrowed : c.borrowed) ch.num_borrowed += is_borrowed;
        ch.large_dropped += e.large_dropped;
    }

    order_borrowed_blocks(ch);
    return true;
}

inline bool build_chunks(Chunked& ch, const std::vector<int>& order, int num_chunks, const Config& cfg) {
    return build_chunks_from_owner(ch, order_owner(ch.global, order, num_chunks), num_chunks, cfg);
}

} // namespace packer

#include "partition.hpp"   // the default partitioner; it builds on everything above. Meow.

namespace packer {

// The smallest K that builds. Partition: the BFS cut refined by cfg.partition_fm_passes of k-way FM
// (#42: 1 pass gives 0.38x the borrowed nodes for 1-8 s on the 8 chunked designs, and reaches the
// minimum K where the bare cut cannot). FM's model counts slot nodes but not bank-row rounding (a
// chunk rounds up to its busiest bank, measured 23-60 slots), so FM aims at a target below capacity;
// a build that fails on rounding tightens the target by the measured overshoot, which carries to the
// next K. Safety net: once FM fits the model but not the builder at two K, it is dropped for this
// design. Each K also tries the bare cut, so the result never needs more chunks than it would. Meow.
inline Chunked encode_chunked(const Netlist& nl, const Config& cfg, long capacity) {
    Chunked ch;
    ch.capacity = capacity;
    resolve_pin_nodes(nl, ch.global);
    const std::vector<int> order = locality_order(ch.global);
    long slot_owners = 0;
    for (size_t node = 0; node < ch.global.kind.size(); node++) slot_owners += needs_slot(ch.global.kind[node]);
    PartitionGraph graph;
    Units units;
    long target = capacity - capacity / 200;
    int model_misses = 0;   // K at which FM fit the model but not the builder
    long fm_from_k = 0;     // FM still over capacity at K: its total load says no K below this can fit
    for (int num_chunks = std::max<long>(1, (slot_owners + capacity - 1) / capacity); num_chunks <= 64; num_chunks++) {
        if (num_chunks > 1 && cfg.partition_fm_passes > 0 && model_misses < 2 && num_chunks >= fm_from_k) {
            bool model_fit = false;
            if (graph.num_nodes == 0) { graph = make_partition_graph(ch.global, cfg); units = level0_units(graph); }
            for (int attempt = 0; attempt < 3; attempt++) {
                PartitionState state;
                state.init(graph, num_chunks, order_owner(ch.global, order, num_chunks));
                FmOptions opt;
                opt.capacity = target;
                opt.max_passes = cfg.partition_fm_passes;
                fm_refine(state, units, opt);
                if (overflow_of(state.load, target) > 0) {   // FM cannot fit even the model at this K
                    long total_load = 0;
                    for (long l : state.load) total_load += l;
                    fm_from_k = std::max<long>(num_chunks + 1, (total_load + target - 1) / target);
                    break;
                }
                model_fit = true;
                if (build_chunks_from_owner(ch, state.owner, num_chunks, cfg)) { ch.fits = true; ch.fm_target = target; return ch; }
                long overshoot = 0;   // slots the builder needed beyond the model's count, worst built chunk
                for (int k = 0; k < num_chunks; k++)
                    if (ch.chunks[k].enc.num_slots > 0) overshoot = std::max(overshoot, ch.chunks[k].enc.num_slots - state.load[k]);
                if (overshoot <= 0) break;   // not a rounding failure (e.g. coloring)
                target -= overshoot;
            }
            model_misses += model_fit;
        }
        if (build_chunks(ch, order, num_chunks, cfg)) { ch.fits = true; return ch; }
    }
    return ch;
}

// One borrowed block's op lists (pin_record.hpp BorrowedBlockDesc), from its entries in emission order: each
// entry's bank and the owner slot it carries; its row is the next of its bank, from the block's
// first row. Send: BORROWED_BEAT writes the next half-row as soon as every bank of that half has put its
// last entry there. Merge: BORROWED_BEAT loads a half-row just before its first entry needs it. Either
// list is infeasible if a buffer row would be reused before it is written out (send) or used (merge)
// -- the block needs more than BORROWED_BUFFER_ROWS rows in flight. Meow.
struct BorrowedOps { std::vector<uint32_t> send, merge; bool feasible = true; long max_rows_in_flight = 0; };

inline BorrowedOps borrowed_block_ops(const std::vector<long>& owner_slots, const std::vector<int>& banks, long rows) {
    BorrowedOps t;
    const long R = BORROWED_BUFFER_ROWS, beats = rows * BANKS / LANES;
    std::vector<long> total(BANKS, 0), placed(BANKS, 0);
    for (int b : banks) total[b]++;
    auto half_final = [&](long beat) {   // every bank of the half has placed its entry for that row, or never will
        const long row = beat * LANES / BANKS, half = beat % (BANKS / LANES);
        for (int b = (int)half * LANES; b < (int)(half + 1) * LANES; b++) if (placed[b] <= row && total[b] > row) return false;
        return true;
    };
    long written = 0;
    for (size_t i = 0; i < banks.size(); i++) {
        const long row = placed[banks[i]];
        if ((row - R + 1) * (BANKS / LANES) > written) t.feasible = false;   // buffer row still holds row - R
        t.max_rows_in_flight = std::max(t.max_rows_in_flight, row + 1 - written * LANES / BANKS);
        t.send.push_back(make_borrowed_entry((uint32_t)owner_slots[i], banks[i]));
        placed[banks[i]]++;
        while (written < beats && half_final(written)) { t.send.push_back(BORROWED_BEAT_OP); written++; }
    }
    while (written < beats) { t.send.push_back(BORROWED_BEAT_OP); written++; }

    std::fill(placed.begin(), placed.end(), 0);   // now: entries consumed per bank
    long loaded = 0;
    for (size_t i = 0; i < banks.size(); i++) {
        const int b = banks[i];
        const long need = placed[b] * (BANKS / LANES) + b / LANES;
        for (; loaded <= need; loaded++) {
            const long row = loaded * LANES / BANKS, half = loaded % (BANKS / LANES);
            for (int k = (int)half * LANES; k < (int)(half + 1) * LANES; k++)
                if (row >= R && placed[k] <= row - R && total[k] > row - R) t.feasible = false;   // row - R not used yet
            t.merge.push_back(BORROWED_BEAT_OP);
        }
        t.merge.push_back(make_borrowed_entry((uint32_t)owner_slots[i], b));
        placed[b]++;
    }
    return t;
}

// Owner-side slot of every owned node, for the borrowed ops. Meow.
inline std::vector<long> owner_slots_of(const Chunked& ch) {
    std::vector<long> owner_slot(ch.global.kind.size(), -1);
    for (int j = 0; j < ch.num_chunks; j++)
        for (size_t l = 0; l < ch.chunks[j].work_node.size(); l++)
            if (!ch.chunks[j].borrowed[l] && ch.owner[ch.chunks[j].work_node[l]] == j)
                owner_slot[ch.chunks[j].work_node[l]] = ch.chunks[j].enc.node_slot[l];
    return owner_slot;
}

inline BorrowedOps block_ops(const Chunked& ch, const Chunk& borrower, const Chunk::BorrowedBlock& block, const std::vector<long>& owner_slot) {
    std::vector<long> slots;
    std::vector<int> banks;
    for (long i = block.first; i < block.first + block.count; i++) {
        const int l = borrower.borrowed_order[i];
        slots.push_back(owner_slot[borrower.work_node[l]]);
        banks.push_back(borrower.enc.bank[l]);
    }
    return borrowed_block_ops(slots, banks, block.rows);
}

// Slot-major position image of chunk k, one axis. Own cells/macros carry their position, fixed
// pins position + offset; macro pins AND borrowed nodes are NaN -- the device must refresh / receive them.
// fill_borrowed (#47 bring-up shortcut, Mark 2026-10-09): borrowed nodes carry their node's real position
// instead, as if every owner's send had already run, so compute_pass can be tested on its own. Meow.
inline std::vector<float> chunk_slot_positions(const Chunked& ch, int k, int axis, const std::vector<float>& node_pos,
                                               bool fill_borrowed = false) {
    const Chunk& c = ch.chunks[k];
    std::vector<float> image = slot_positions(c.enc, axis, node_pos);
    if (!fill_borrowed)
        for (size_t l = 0; l < c.borrowed.size(); l++) if (c.borrowed[l]) image[c.enc.node_slot[l]] = NAN;
    return image;
}

// Every per-chunk array the device reads, for one axis, concatenated in chunk order and located
// by the descriptors. All chunks share the global offset tables, hence one offset_bits. Meow.
struct ChunkedDevice {
    std::vector<ChunkDesc>        desc;
    std::vector<int32_t>          group_ends;     // NUM_GROUP_ENDS per chunk: degree_group_end, then span_group_end
    std::vector<uint32_t>         records;        // LANES per beat
    std::vector<float>            slot_images;    // slot-major, each chunk a whole number of beats
    std::vector<MacroPinRef>      macro_pins;
    std::vector<BorrowedBlockDesc> borrowed_blocks;  // per owner, borrowers ascending (ChunkDesc first_borrowed_block, num_borrowers)
    std::vector<uint32_t>          borrowed_ops;     // every borrowed block's send and merge ops
    std::vector<float>            offset_table;
    int                           offset_bits = 0;
};

inline ChunkedDevice chunked_device_arrays(const Chunked& ch, int axis, const std::vector<float>& node_pos,
                                           bool fill_borrowed = false) {   // see chunk_slot_positions
    ChunkedDevice dev;
    dev.offset_table  = ch.global.offset_table[axis];
    dev.offset_bits   = ch.chunks[0].enc.offset_bits;
    std::vector<long> slot_beat_offset(ch.num_chunks + 1, 0);
    for (int k = 0; k < ch.num_chunks; k++) slot_beat_offset[k + 1] = slot_beat_offset[k] + ch.chunks[k].enc.num_slots / LANES;
    const std::vector<long> owner_slot = owner_slots_of(ch);
    for (int k = 0; k < ch.num_chunks; k++) {
        const Chunk& c = ch.chunks[k];
        if (c.enc.offset_bits != dev.offset_bits) { fprintf(stderr, "chunks disagree on offset_bits\n"); exit(2); }
        ChunkDesc d = {};
        d.record_beat_offset   = (int32_t)(dev.records.size() / LANES);
        d.num_beats            = (int32_t)c.enc.issue.size();
        dev.group_ends.insert(dev.group_ends.end(), c.enc.degree_group_end.begin(), c.enc.degree_group_end.end());
        dev.group_ends.insert(dev.group_ends.end(), c.enc.span_group_end.begin(), c.enc.span_group_end.end());
        d.slot_beat_offset     = (int32_t)(dev.slot_images.size() / LANES);
        d.num_slot_beats       = (int32_t)(c.enc.num_slots / LANES);
        d.first_private_slot   = (int32_t)c.enc.first_private_slot;
        d.first_borrowed_slot  = (int32_t)c.enc.first_borrowed_slot;
        d.first_fixed_slot     = (int32_t)c.enc.first_fixed_slot;
        d.macro_pin_offset     = (int32_t)dev.macro_pins.size();
        d.num_macro_pins       = (int32_t)c.enc.macro_order.size();   // the scheduled list, SKIP padding included
        d.first_borrowed_block = (int32_t)dev.borrowed_blocks.size();
        for (int borrower = 0; borrower < ch.num_chunks; borrower++)   // this chunk as owner (C7)
            for (const Chunk::BorrowedBlock& block : ch.chunks[borrower].borrowed_blocks) {
                if (block.owner != k) continue;
                const BorrowedOps ops = block_ops(ch, ch.chunks[borrower], block, owner_slot);
                if (!ops.feasible) { fprintf(stderr, "borrowed block %d -> %d needs more than %d buffer rows\n", k, borrower, BORROWED_BUFFER_ROWS); exit(2); }
                BorrowedBlockDesc block_desc;
                block_desc.send_op_offset = (int32_t)dev.borrowed_ops.size();
                block_desc.num_send_ops = (int32_t)ops.send.size();
                dev.borrowed_ops.insert(dev.borrowed_ops.end(), ops.send.begin(), ops.send.end());
                block_desc.merge_op_offset = (int32_t)dev.borrowed_ops.size();
                block_desc.num_merge_ops = (int32_t)ops.merge.size();
                dev.borrowed_ops.insert(dev.borrowed_ops.end(), ops.merge.begin(), ops.merge.end());
                block_desc.block_beat_offset = (int32_t)(slot_beat_offset[borrower] + block.start_row * BANKS / LANES);
                dev.borrowed_blocks.push_back(block_desc);
            }
        d.num_borrowers        = (int32_t)dev.borrowed_blocks.size() - d.first_borrowed_block;
        dev.desc.push_back(d);

        dev.records.insert(dev.records.end(), c.enc.records[axis].begin(), c.enc.records[axis].end());
        const std::vector<float> image = chunk_slot_positions(ch, k, axis, node_pos, fill_borrowed);
        dev.slot_images.insert(dev.slot_images.end(), image.begin(), image.end());
        const std::vector<MacroPinRef> refs = macro_pin_refs(c.enc, axis);
        dev.macro_pins.insert(dev.macro_pins.end(), refs.begin(), refs.end());
    }
    return dev;
}

// Every chunk's encoded large nets, as global (= parsed) net ids. Meow.
inline std::vector<int> chunked_large_nets(const Chunked& ch) {
    std::vector<int> large_nets;
    for (const Chunk& c : ch.chunks) for (int local : c.enc.large_nets) large_nets.push_back(c.net_global[local]);
    return large_nets;
}

// Replays one block's send and merge ops on NODE IDS, exactly as the device moves floats (row buffer,
// per-bank row counters, half-row beats), and checks that every borrower slot of the block receives
// its own node from the owner, and that every merge entry adds that node into its owner slot. A buffer
// row reused too early, a wrong bank or row, or a beat out of place all show as a wrong node. Meow.
inline bool borrowed_ops_deliver(const Chunked& ch, const Chunk& borrower, const Chunk::BorrowedBlock& block, const BorrowedOps& ops) {
    const Chunk& owner = ch.chunks[block.owner];
    std::vector<int> owner_node(owner.enc.num_slots, -1), borrower_node(borrower.enc.num_slots, -1);
    for (size_t l = 0; l < owner.work_node.size(); l++) if (owner.enc.node_slot[l] >= 0) owner_node[owner.enc.node_slot[l]] = owner.work_node[l];
    for (size_t l = 0; l < borrower.work_node.size(); l++) if (borrower.enc.node_slot[l] >= 0) borrower_node[borrower.enc.node_slot[l]] = borrower.work_node[l];
    const long R = BORROWED_BUFFER_ROWS, first_slot = block.start_row * BANKS;
    std::vector<int> buffer(BANKS * R, -1), row(BANKS, 0);
    long beat = 0;
    for (uint32_t op : ops.send) {
        if (op & BORROWED_BEAT_OP) {
            const long r = beat * LANES / BANKS, half = beat % (BANKS / LANES);
            for (int j = 0; j < LANES; j++) {
                const int b = (int)half * LANES + j;
                const long slot = first_slot + beat * LANES + j;
                const int want = slot < borrower.enc.num_slots ? borrower_node[slot] : -2;
                if (want >= 0 && buffer[b * R + r % R] != want) return false;   // a real slot got the wrong node
            }
            beat++;
        } else {
            const int b = borrowed_entry_bank(op);
            buffer[b * R + row[b]++ % R] = owner_node[borrowed_entry_slot(op)];
        }
    }
    if (beat != block.rows * BANKS / LANES) return false;
    std::fill(buffer.begin(), buffer.end(), -1);
    std::fill(row.begin(), row.end(), 0);
    beat = 0;
    for (uint32_t op : ops.merge) {
        if (op & BORROWED_BEAT_OP) {
            const long r = beat * LANES / BANKS, half = beat % (BANKS / LANES);
            for (int j = 0; j < LANES; j++) buffer[((int)half * LANES + j) * R + r % R] = borrower_node[first_slot + beat * LANES + j];
            beat++;
        } else {
            const int b = borrowed_entry_bank(op);
            const int node = buffer[b * R + row[b]++ % R];
            if (node < 0 || node != owner_node[borrowed_entry_slot(op)]) return false;   // adds another node's gradient
        }
    }
    return true;
}

// Chunked checker: every chunk decodes on its own (local slots -> parsed geometry); the union must
// be the in-scope netlist exactly. Plus ownership, capacity, the borrowed layout (C7), and the borrowed ops
// replayed on node ids: every borrowed node's slot receives the node its borrower expects, and every merge
// adds into that node's owner slot. Meow.
inline int check_chunked(const Netlist& nl, const Chunked& ch, const Config& cfg) {
    Failures fail;
    if (!ch.fits) { fail("no chunk count fits the capacity", ch.capacity); return fail.count; }
    std::vector<std::vector<GeomPin>> decoded;
    std::vector<int> own_count(ch.global.kind.size(), 0);
    std::vector<long> chunk_slot_nodes;
    for (int k = 0; k < ch.num_chunks; k++) {
        const Chunk& c = ch.chunks[k];
        decode_stream(c.enc, cfg, fail, decoded);
        if (c.enc.num_slots > ch.capacity) fail("chunk exceeds capacity", k);
        std::vector<int> local_of(ch.global.kind.size(), -1);
        for (size_t l = 0; l < c.work_node.size(); l++) local_of[c.work_node[l]] = (int)l;
        std::vector<char> refreshed(c.work_node.size(), 0);
        for (const MacroPin& mp : c.enc.macro_pins) refreshed[mp.pin_node] = 1;
        long slot_nodes = 0;
        for (size_t l = 0; l < c.work_node.size(); l++) {
            const int node = c.work_node[l];
            const bool pin = ch.global.kind[node] == MACRO_PIN;
            if (!c.borrowed[l] && ch.global.work.movable[node]) {
                if (ch.owner[node] == k) own_count[node]++;
                else if (!pin) fail("own node of another chunk", node);
                else if (local_of[ch.global.base_node[node]] < 0 || !c.borrowed[local_of[ch.global.base_node[node]]])
                    fail("macro pin of another chunk without its borrowed macro (C6)", node);
            }
            if (c.borrowed[l] && pin) fail("borrowed macro pin (C6)", node);
            if (c.borrowed[l] && ch.owner[node] == k) fail("borrowed node that this chunk owns", node);
            if (pin && !refreshed[l]) fail("macro pin missing from its chunk's refresh list", node);
            if (c.enc.node_slot[l] < 0) fail("local node without a slot", node);
            slot_nodes += needs_slot(ch.global.kind[node]);
        }
        chunk_slot_nodes.push_back(slot_nodes);
    }
    {   // C7: every borrowed node in its chunk's borrowed rows, each bank's borrowed rows in emission order. Meow.
        const std::vector<long> owner_slot = owner_slots_of(ch);
        for (int k = 0; k < ch.num_chunks; k++) {
            const Chunk& c = ch.chunks[k];
            const Encoded& e = c.enc;
            long num_borrowed = 0;
            for (size_t l = 0; l < c.work_node.size(); l++) {
                const long slot = e.node_slot[l];
                if (c.borrowed[l]) {
                    num_borrowed++;
                    if (slot < e.first_borrowed_slot || slot >= e.first_fixed_slot) fail("borrowed node outside its chunk's borrowed rows (C7)", k);
                } else if (slot_class(e.kind[l]) == 1 && slot >= e.first_borrowed_slot) {
                    fail("owned movable slot in the borrowed rows (C7)", k);
                }
            }
            if ((long)c.borrowed_order.size() != num_borrowed) { fail("borrowed_order does not list every borrowed node (C7)", k); continue; }
            for (size_t i = 0; i < c.borrowed_order.size(); i++) {
                const int l = c.borrowed_order[i];
                if (!c.borrowed[l]) { fail("borrowed_order names a node that is not borrowed (C7)", k); break; }
                if (i > 0) {
                    const int a = c.work_node[c.borrowed_order[i - 1]], b = c.work_node[l];
                    if (ch.owner[a] > ch.owner[b] || (ch.owner[a] == ch.owner[b] && owner_slot[a] >= owner_slot[b]))
                        { fail("borrowed_order not in emission order (C7)", k); break; }
                }
            }
            long row = e.first_borrowed_slot / BANKS, covered = 0;
            for (const Chunk::BorrowedBlock& block : c.borrowed_blocks) {
                if (block.start_row != row || block.first != covered) { fail("borrowed blocks not contiguous, fresh rows (C7)", k); break; }
                std::vector<long> next_row(BANKS, block.start_row);
                for (long i = block.first; i < block.first + block.count; i++) {
                    const int l = c.borrowed_order[i];
                    if (ch.owner[c.work_node[l]] != block.owner) { fail("a borrowed block holds another owner's node (C7)", k); break; }
                    if (e.node_slot[l] != next_row[e.bank[l]]++ * BANKS + e.bank[l]) { fail("a bank's rows in a borrowed block not in emission order (C7)", k); break; }
                }
                if (*std::max_element(next_row.begin(), next_row.end()) - block.start_row != block.rows) fail("borrowed block row count (C7)", k);
                const BorrowedOps ops = block_ops(ch, c, block, owner_slot);
                if (!ops.feasible) fail("borrowed block needs more than BORROWED_BUFFER_ROWS rows in flight (C7)", k);
                if (!borrowed_ops_deliver(ch, c, block, ops)) fail("borrowed ops deliver a node to the wrong slot (C7)", k);
                row += block.rows;
                covered += block.count;
            }
            if (covered != num_borrowed || row * BANKS != e.first_fixed_slot) fail("borrowed blocks do not cover the borrowed rows (C7)", k);
        }
    }
    {   // C8: shared marks exactly the owned nodes some chunk borrows, and all of them sit below
        // first_private_slot, a whole row count inside the owned rows. Meow.
        std::vector<char> borrowed_anywhere(ch.global.kind.size(), 0);
        for (const Chunk& c : ch.chunks)
            for (size_t l = 0; l < c.work_node.size(); l++) if (c.borrowed[l]) borrowed_anywhere[c.work_node[l]] = 1;
        for (int k = 0; k < ch.num_chunks; k++) {
            const Chunk& c = ch.chunks[k];
            const Encoded& e = c.enc;
            if (e.first_private_slot % BANKS || e.first_private_slot > e.first_borrowed_slot) fail("first_private_slot not a row inside the owned rows (C8)", k);
            for (size_t l = 0; l < c.work_node.size(); l++) {
                if (c.borrowed[l]) continue;
                const bool is_borrowed = borrowed_anywhere[c.work_node[l]];
                if ((!e.shared.empty() && e.shared[l]) != is_borrowed) { fail("shared flag differs from the borrowed set (C8)", k); break; }
                if (is_borrowed && e.node_slot[l] >= e.first_private_slot) { fail("borrowed node above first_private_slot (C8)", k); break; }
            }
        }
    }
    {   // The partition model (partition.hpp) must count what the builder built, C6 included. Meow.
        const PartitionGraph G = make_partition_graph(ch.global, cfg);
        PartitionState s;
        s.init(G, ch.num_chunks, ch.owner);
        if (s.num_borrowed != ch.num_borrowed) fail("partition model's borrowed-node count differs from the builder's", s.num_borrowed - ch.num_borrowed);
        for (int k = 0; k < ch.num_chunks; k++)
            if (s.load[k] != chunk_slot_nodes[k]) fail("partition model's load differs from the builder's", k);
    }
    for (size_t node = 0; node < own_count.size(); node++)
        if (needs_slot(ch.global.kind[node]) && ch.global.work.movable[node] && own_count[node] != 1) fail("movable node not owned exactly once", (long)node);
    for (const MacroPin& mp : ch.global.macro_pins)
        if (ch.owner[mp.pin_node] != ch.owner[mp.macro_node]) fail("macro pin split from its macro", mp.pin_node);

    std::sort(decoded.begin(), decoded.end());
    if (decoded != in_scope_geometry(nl, chunked_large_nets(ch))) fail("decoded chunks differ from the input netlist", (long)decoded.size());
    return fail.count;
}

} // namespace packer

#endif // BEAT_PACKER_HPP
