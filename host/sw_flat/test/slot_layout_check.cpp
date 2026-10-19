// slot_layout_check -- #45 step 4's gate: the whole-design slot layout (slot_layout.hpp).
//
//   slot_layout_check <design_dir> <target_density>     exit 0 only if every check passes
//
// <target_density> is the configured maximum_utilization (benchmarks.py's column); the benchmark's
// placement.constraints overrides it, as in sw_only. Checks, each a verdict:
//   [fillers]   make_fillers vs sw_only's DataBase::addFillers on the same design, macro tags set from
//               tag_movable_macros (flat_database_check holds those equal to sw_only's): count, width,
//               height and the effective target density, bit-equal.
//   [layout]    the invariants the density path relies on, checked from the layout alone (plus the
//               chunk ranges): every movable node owns exactly one slot and home_slot names it; every
//               filler owns one tail slot; a BORROWED slot is a copy of a node owned in ANOTHER chunk
//               and sits in its chunk's tail rows; pin slots point at a movable macro / a fixed node or
//               IO pad; only OWNED and FILLER slots carry a size, and it is the node's / the filler's.
//   [self-test] six corruptions of the layout and one of the fillers; each must fail its check.
//               Proves the checks can fail at all.
// The "chunks, borrowed" line is the chunking, compared with the step-3 sweep by slot_layout_check.sh.
// Meow.

#include "fillers.hpp"
#include "flat_database.hpp"
#include "flat_netlist.hpp"
#include "slot_layout.hpp"

#include "DataBase.h"
#include "Logger.h"

#include <cstring>
#include <string>

using namespace AIEplace;
using namespace sw_flat;

namespace {

constexpr long SLOT_CAPACITY = 1L << 20;   // pl/src/modules/hpwl_gradient/slot_arrays.hpp SLOT_CAPACITY

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string fillers_difference(const Fillers& ours, const Fillers& golden)
{
    if (ours.count != golden.count) return "count " + std::to_string(ours.count) + " vs " + std::to_string(golden.count);
    if (!same_bits(ours.target_density, golden.target_density))
        return "target density " + std::to_string(ours.target_density) + " vs " + std::to_string(golden.target_density);
    if (ours.count == 0) return "";   // DataBase has no filler to read a size from; none is laid out
    if (!same_bits(ours.width, golden.width)) return "width " + std::to_string(ours.width) + " vs " + std::to_string(golden.width);
    if (!same_bits(ours.height, golden.height)) return "height " + std::to_string(ours.height) + " vs " + std::to_string(golden.height);
    return "";
}

// DataBase::addFillers, the golden. It reads isMovableMacro, which sw_only's Placer sets. Meow.
Fillers database_fillers(const std::string& dir, const FlatDesign& design, const std::vector<char>& movable_macro, float configured)
{
    DataBase db(dir);
    for (int node = 0; node < design.num_nodes(); node++) {
        if (design.node_kind[node] == NodeKind::IO_PAD) continue;
        db.getComponents().at(design.node_name[node])->setMovableMacro(movable_macro[node]);
    }
    float target_density = db.getMaximumUtilization() > 0.0f ? db.getMaximumUtilization() : configured;
    Fillers fillers;
    fillers.target_density = db.addFillers(target_density);
    fillers.count = (int)db.getFillers().size();
    if (fillers.count) {
        fillers.width = db.getFillers()[0]->getXsize();
        fillers.height = db.getFillers()[0]->getYsize();
    }
    return fillers;
}

std::string layout_difference(const FlatDesign& design, const std::vector<char>& movable_macro,
                              const packer::Chunked& chunked, const Fillers& fillers, const SlotLayout& layout)
{
    auto at = [](const char* what, long slot) { return std::string(what) + " at slot " + std::to_string(slot); };
    if (layout.num_chunks != chunked.num_chunks || (int)layout.chunk_first_slot.size() != chunked.num_chunks + 1)
        return "chunk count";
    if (layout.chunk_first_slot[0] != 0) return "chunk 0 does not start at slot 0";
    for (int k = 0; k < chunked.num_chunks; k++)
        if (layout.chunk_first_slot[k + 1] - layout.chunk_first_slot[k] != chunked.chunks[k].enc.num_slots)
            return "chunk " + std::to_string(k) + " range differs from its num_slots";
    if (layout.first_filler_slot != layout.chunk_first_slot.back()) return "filler tail does not follow the chunks";
    if (layout.num_slots % pinrec::BANKS) return "num_slots not a multiple of BANKS";
    if (layout.num_slots < layout.first_filler_slot + fillers.count) return "filler tail does not fit num_slots";
    for (size_t size : {layout.role.size(), layout.node.size(), layout.width.size(), layout.height.size()})
        if ((long)size != layout.num_slots) return "a per-slot array is not num_slots long";
    if ((int)layout.home_slot.size() != design.num_nodes()) return "home_slot is not num_nodes long";

    auto chunk_of = [&](long slot) {
        int k = 0;
        while (k < layout.num_chunks && slot >= layout.chunk_first_slot[k + 1]) k++;
        return k;   // num_chunks = the filler tail
    };
    std::vector<int> owned_count(design.num_nodes(), 0);
    int chunk = 0;
    for (long slot = 0; slot < layout.num_slots; slot++) {
        while (chunk < layout.num_chunks && slot >= layout.chunk_first_slot[chunk + 1]) chunk++;
        const SlotRole role = layout.role[slot];
        const int node = layout.node[slot];
        const bool in_tail = slot >= layout.first_filler_slot && slot < layout.first_filler_slot + fillers.count;
        if (in_tail != (role == SlotRole::FILLER)) return at("filler tail and FILLER role disagree", slot);
        const bool deposits = role == SlotRole::OWNED || role == SlotRole::FILLER;
        if (!deposits && (layout.width[slot] != 0.0f || layout.height[slot] != 0.0f)) return at("size on a slot that does not deposit", slot);
        if (role == SlotRole::EMPTY) {
            if (node != -1) return at("EMPTY slot names a node", slot);
            continue;
        }
        if (role == SlotRole::FILLER) {
            if (node != slot - layout.first_filler_slot) return at("filler index", slot);
            if (!same_bits(layout.width[slot], fillers.width) || !same_bits(layout.height[slot], fillers.height)) return at("filler size", slot);
            continue;
        }
        if (chunk >= layout.num_chunks) return at("a node slot in the filler tail", slot);
        if (node < 0 || node >= design.num_nodes()) return at("node out of range", slot);
        const packer::Encoded& enc = chunked.chunks[chunk].enc;
        const long local = slot - layout.chunk_first_slot[chunk];
        const bool movable = design.node_kind[node] == NodeKind::CELL;
        switch (role) {
        case SlotRole::OWNED:
            if (!movable) return at("OWNED slot of a fixed node or IO pad", slot);
            if (local >= enc.first_tail_slot) return at("OWNED slot outside its chunk's owned rows", slot);
            if (!same_bits(layout.width[slot], design.node_width[node]) || !same_bits(layout.height[slot], design.node_height[node]))
                return at("OWNED size is not the node's", slot);
            owned_count[node]++;
            break;
        case SlotRole::BORROWED:
            if (!movable) return at("BORROWED slot of a fixed node or IO pad", slot);
            if (local < enc.first_tail_slot || local >= enc.first_fixed_slot) return at("BORROWED slot outside its chunk's tail rows", slot);
            if (layout.home_slot[node] < 0 || chunk_of(layout.home_slot[node]) == chunk) return at("BORROWED slot not owned by another chunk", slot);
            break;
        case SlotRole::MACRO_PIN:
            if (!movable || !movable_macro[node]) return at("MACRO_PIN slot of a node that is not a movable macro", slot);
            if (local >= enc.first_fixed_slot) return at("MACRO_PIN slot in fixed rows", slot);
            break;
        case SlotRole::FIXED_PIN:
            if (movable) return at("FIXED_PIN slot of a movable node", slot);
            if (local < enc.first_fixed_slot) return at("FIXED_PIN slot in movable rows", slot);
            break;
        default: return at("unknown role", slot);
        }
    }
    for (int node = 0; node < design.num_nodes(); node++) {
        const bool movable = design.node_kind[node] == NodeKind::CELL;
        if (owned_count[node] != (movable ? 1 : 0))
            return "node " + design.node_name[node] + " owns " + std::to_string(owned_count[node]) + " slots";
        const long home = layout.home_slot[node];
        if (movable ? (home < 0 || home >= layout.num_slots || layout.role[home] != SlotRole::OWNED || layout.node[home] != node)
                    : home != -1)
            return "home_slot of node " + design.node_name[node];
    }
    return "";
}

// Each corruption must be caught; returns the number that were NOT. Meow.
int self_test(const FlatDesign& design, const std::vector<char>& movable_macro, const packer::Chunked& chunked,
              const Fillers& fillers, const Fillers& golden, const SlotLayout& layout)
{
    auto first_of = [&](SlotRole role) {
        for (long slot = 0; slot < layout.num_slots; slot++) if (layout.role[slot] == role) return slot;
        return -1L;
    };
    const long owned = first_of(SlotRole::OWNED), borrowed = first_of(SlotRole::BORROWED);
    const long fixed_pin = first_of(SlotRole::FIXED_PIN), filler = first_of(SlotRole::FILLER);
    long second_owned = -1;
    for (long slot = owned + 1; owned >= 0 && slot < layout.num_slots && second_owned < 0; slot++)
        if (layout.role[slot] == SlotRole::OWNED) second_owned = slot;

    int missed = 0, tried = 0;
    auto expect_fail = [&](const char* name, auto corrupt) {
        SlotLayout bad = layout;
        corrupt(bad);
        tried++;
        if (layout_difference(design, movable_macro, chunked, fillers, bad).empty()) { printf("  self-test MISSED: %s\n", name); missed++; }
    };
    if (second_owned >= 0) expect_fail("two slots own one node", [&](SlotLayout& bad) { bad.node[second_owned] = bad.node[owned]; });
    if (owned >= 0) expect_fail("an owned size zeroed", [&](SlotLayout& bad) { bad.width[owned] = 0.0f; });
    if (borrowed >= 0) expect_fail("a borrowed node marked owned", [&](SlotLayout& bad) { bad.role[borrowed] = SlotRole::OWNED; });
    if (fixed_pin >= 0 && owned >= 0) expect_fail("a fixed pin names a cell", [&](SlotLayout& bad) { bad.node[fixed_pin] = bad.node[owned]; });
    if (filler >= 0) expect_fail("a filler dropped", [&](SlotLayout& bad) { bad.role[filler] = SlotRole::EMPTY; bad.width[filler] = bad.height[filler] = 0.0f; bad.node[filler] = -1; });
    if (owned >= 0) expect_fail("a pin slot given a size", [&](SlotLayout& bad) {
        for (long slot = 0; slot < bad.num_slots; slot++)
            if (bad.role[slot] == SlotRole::FIXED_PIN || bad.role[slot] == SlotRole::MACRO_PIN) { bad.width[slot] = 1.0f; break; }
    });
    Fillers bad_fillers = fillers;
    bad_fillers.count++;
    tried++;
    if (fillers_difference(bad_fillers, golden).empty()) { printf("  self-test MISSED: filler count + 1\n"); missed++; }
    printf("  [self-test] %d of %d corruptions caught\n", tried - missed, tried);
    return missed;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) { fprintf(stderr, "usage: slot_layout_check <design_dir> <target_density>\n"); return 2; }
    Logger::setup_logging(LogLevel::WARNING);
    std::string dir = argv[1];
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    const std::string suite = dir.substr(0, dir.find_last_of('/'));
    const std::string tag = suite.substr(suite.find_last_of('/') + 1) + "/" + dir.substr(dir.find_last_of('/') + 1);
    const float configured_density = std::stof(argv[2]);

    FlatDatabase flat_db;
    std::string why;
    if (!read_flat_design(dir, flat_db, why)) { printf("FAIL %s: FlatDatabase: %s\n", tag.c_str(), why.c_str()); return 1; }
    const FlatDesign& design = flat_db.design;
    const std::vector<char> movable_macro = tag_movable_macros(design);

    std::vector<std::string> failures;
    const Fillers fillers = make_fillers(design, movable_macro, effective_target_density(design, configured_density));
    const Fillers golden = database_fillers(dir, design, movable_macro, configured_density);
    std::string difference = fillers_difference(fillers, golden);
    if (!difference.empty()) failures.push_back("[fillers] " + difference);

    const packer::Netlist netlist = to_netlist(design, movable_macro, tag);
    const packer::Config cfg;
    const packer::Chunked chunked = packer::encode_chunked(netlist, cfg, SLOT_CAPACITY);
    if (!chunked.fits) failures.push_back("[layout] encode_chunked found no chunking");
    else {
        const SlotLayout layout = build_slot_layout(design, chunked, fillers);
        difference = layout_difference(design, movable_macro, chunked, fillers, layout);
        if (!difference.empty()) failures.push_back("[layout] " + difference);
        if (self_test(design, movable_macro, chunked, fillers, golden, layout)) failures.push_back("[self-test] a corruption passed");

        std::vector<char> on_net(design.num_nodes(), 0);
        for (const auto& net : netlist.nets) for (const auto& pin : net) on_net[pin.node] = 1;
        long netless = 0, movable = 0;
        for (int node = 0; node < design.num_nodes(); node++)
            if (design.node_kind[node] == NodeKind::CELL) { movable++; netless += !on_net[node]; }
        printf("  %d chunks, %ld borrowed\n", chunked.num_chunks, chunked.num_borrowed);
        printf("  [info] %ld slots: %ld in chunks (%ld movable nodes, %ld netless), %d fillers of %g x %g, target density %g\n",
               layout.num_slots, layout.first_filler_slot, movable, netless, fillers.count, fillers.width, fillers.height,
               fillers.target_density);
    }
    for (const std::string& failure : failures) printf("  %s\n", failure.c_str());
    printf("%s %s\n", failures.empty() ? "PASS" : "FAIL", tag.c_str());
    return failures.empty() ? 0 : 1;
}
