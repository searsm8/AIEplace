// flat_database_check -- #45 step 3's gate: the one parse (FlatDatabase) must reproduce what sw_only reads.
//
//   flat_database_check [--no-legacy] <design_dir> [...]     exit 0 only if every design passes
//
// --no-legacy skips [legacy] for inputs the legacy reader rejects outright (it exits): the parser's
// hand-written edge_cases/, which are the only inputs with COVER, PLACED non-CORE/BLOCK and .pl
// /FIXED on a non-terminal -- none of the 44 designs has one.
//
// Per design, three checks, each a verdict (any difference fails the design):
//   [database] FlatDesign vs sw_only's DataBase, read from the same files: header, every node (kind,
//              position, size), every net in file order (name, pins, offsets). Floats bit-equal.
//   [macros]   tag_movable_macros vs sw_only's Placer::tagMovableMacros rule run over DataBase in
//              ITS order (a std::map, so name order): equal tag sets.
//   [legacy]   to_netlist vs the legacy packer reader (native_netlist.hpp) that check-reader used:
//              same kept nets and pins; a movable flag differs exactly on the nodes FlatDatabase lists
//              as COVER / PLACED-promoted / .pl-fixed; offsets equal under the convention change
//              (DEF: x units_per_micron; Bookshelf: + half the node size). DEF offsets may differ by
//              <= 2 ulp, counted: the legacy reader rounds a LEF pin centre as ((float)xl + (float)xh)/2,
//              DataBase (and so FlatDatabase, held to it by [database]) as (float)(xl + xh)/2.
// Lines marked [info] are measurements, not verdicts: the macro-rule difference against the legacy
// reader (Mark, 2026-10-07: the packer adopts sw_only's rule) and the offset-table sizes. Meow.

#include "flat_database.hpp"
#include "flat_netlist.hpp"
#include "native_netlist.hpp"

#include "DataBase.h"
#include "Logger.h"

#include <chrono>
#include <climits>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>

using namespace AIEplace;
using sw_flat::FlatDesign;
using sw_flat::NodeKind;

namespace {

double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }
std::string hexf(float v) { char buf[48]; std::snprintf(buf, sizeof buf, "%a", v); return buf; }

#define EXPECT_BITS(what, a, b) \
    do { if (!same_bits((a), (b))) return std::string(what) + ": flat " + hexf(a) + " vs DataBase " + hexf(b); } while (0)

// ---- [database] Meow.
std::string database_difference(DataBase& db, const FlatDesign& flat, std::vector<Node*>& db_node)
{
    if (flat.name != db.getDesignName()) return "design name " + flat.name + " vs " + db.getDesignName();
    if (flat.units_per_micron != db.getUnitsPerMicron()) return "units_per_micron";
    Box& die = db.getDieArea();
    EXPECT_BITS("die_xl", flat.die_xl, die.getPosBottomLeft().x);
    EXPECT_BITS("die_yl", flat.die_yl, die.getPosBottomLeft().y);
    EXPECT_BITS("die_xh", flat.die_xh, die.getPosTopRight().x);
    EXPECT_BITS("die_yh", flat.die_yh, die.getPosTopRight().y);
    EXPECT_BITS("die_shift_x", flat.die_shift_x, db.getDieShift().x);
    EXPECT_BITS("die_shift_y", flat.die_shift_y, db.getDieShift().y);
    EXPECT_BITS("row_height", flat.row_height, db.getRowHeight());
    EXPECT_BITS("site_width", flat.site_width, db.getSiteWidth());
    EXPECT_BITS("maximum_utilization", flat.maximum_utilization, db.getMaximumUtilization());

    const auto& components = db.getComponents();
    const auto& iopads = db.getIOPads();
    if ((size_t)flat.num_nodes() != components.size() + iopads.size())
        return "node count " + std::to_string(flat.num_nodes()) + " vs " + std::to_string(components.size() + iopads.size());
    db_node.assign(flat.num_nodes(), nullptr);
    for (int node = 0; node < flat.num_nodes(); node++) {
        const std::string& name = flat.node_name[node];
        Node* other = nullptr;
        if (flat.node_kind[node] == NodeKind::IO_PAD) {
            auto it = iopads.find(name);
            if (it != iopads.end()) other = it->second;
        } else {
            auto it = components.find(name);
            if (it != components.end()) other = it->second;
        }
        if (!other) return "node " + name + " missing from DataBase";
        db_node[node] = other;
        bool flat_fixed = flat.node_kind[node] == NodeKind::FIXED;
        bool db_fixed = other->getStatus() == FIXED;
        if (flat.node_kind[node] != NodeKind::IO_PAD && flat_fixed != db_fixed) return "node " + name + " fixed status";
        EXPECT_BITS("node " + name + " x", flat.node_x[node], other->next.node_pos.x);
        EXPECT_BITS("node " + name + " y", flat.node_y[node], other->next.node_pos.y);
        EXPECT_BITS("node " + name + " width", flat.node_width[node], other->getXsize());
        EXPECT_BITS("node " + name + " height", flat.node_height[node], other->getYsize());
    }

    const auto& nets = db.getNetsVector();
    if ((size_t)flat.num_nets() != nets.size()) return "net count " + std::to_string(flat.num_nets()) + " vs " + std::to_string(nets.size());
    for (int net = 0; net < flat.num_nets(); net++) {
        Net* other = nets[net];
        if (flat.net_name[net] != other->getName()) return "net " + std::to_string(net) + " name";
        const auto& pins = other->getPins();
        if ((size_t)flat.net_degree(net) != pins.size() || flat.net_degree(net) != other->getDegree())
            return "net " + flat.net_name[net] + " degree";
        for (int k = 0; k < flat.net_degree(net); k++) {
            int pin = flat.net_first_pin[net] + k;
            if (db_node[flat.pin_node[pin]] != pins[k].node_p) return "net " + flat.net_name[net] + " pin " + std::to_string(k) + " node";
            EXPECT_BITS("net " + flat.net_name[net] + " pin offset x", flat.pin_offset_x[pin], pins[k].offset.x);
            EXPECT_BITS("net " + flat.net_name[net] + " pin offset y", flat.pin_offset_y[pin], pins[k].offset.y);
        }
    }
    return "";
}

// ---- [macros]: Placer::tagMovableMacros (host/src/sw_only/src/placer/Setup.cpp:229), copied over
// DataBase in its own iteration order -- the order sw_only actually sums height_sum in. Meow.
std::set<std::string> sw_only_movable_macros(DataBase& db)
{
    std::vector<float> movable_areas;
    float height_sum = 0.0f;
    for (const auto& item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        movable_areas.push_back(item.second->getArea());
        height_sum += item.second->getYsize();
    }
    std::set<std::string> tagged;
    if (movable_areas.empty()) return tagged;
    float row_height = height_sum / movable_areas.size();
    std::vector<float> ascending_areas = movable_areas;
    std::sort(ascending_areas.begin(), ascending_areas.end());
    size_t small_count = std::max<size_t>(1, (size_t)(ascending_areas.size() * 0.999));
    float mean_small_area = std::accumulate(ascending_areas.begin(), ascending_areas.begin() + small_count, 0.0f) / small_count;
    float macro_area_threshold = 10.0f * mean_small_area;
    for (const auto& item : db.getComponents()) {
        Component* comp_p = item.second;
        if (comp_p->getStatus() == FIXED) continue;
        bool is_tall  = comp_p->getYsize() > 2.01f * row_height;
        bool is_large = comp_p->getArea() > macro_area_threshold;
        bool is_sized = comp_p->getXsize() > 1e-4f && comp_p->getYsize() > 1e-4f;
        if (is_tall && is_large && is_sized) tagged.insert(item.first);
    }
    return tagged;
}

std::string macros_difference(DataBase& db, const FlatDesign& flat, const std::vector<char>& movable_macro, int& count)
{
    std::set<std::string> expected = sw_only_movable_macros(db), got;
    for (int node = 0; node < flat.num_nodes(); node++) if (movable_macro[node]) got.insert(flat.node_name[node]);
    count = (int)got.size();
    if (got == expected) return "";
    return std::to_string(got.size()) + " tagged vs sw_only's " + std::to_string(expected.size());
}

// ---- [legacy]. The legacy node ids: components in file order (Bookshelf: every node), then each
// DEF IO pin when a net -- kept or not -- first names it. Meow.
struct LegacyComparison {
    std::string difference;
    long macro_only_flat = 0, macro_only_legacy = 0, macro_both = 0;
    long centre_rounding_pins = 0;   // DEF pin offsets 1-2 ulp off the legacy value, see the header
};

long ulps_apart(float a, float b)
{
    int32_t ia, ib;
    std::memcpy(&ia, &a, sizeof ia); std::memcpy(&ib, &b, sizeof ib);
    if ((ia < 0) != (ib < 0)) return a == b ? 0 : LONG_MAX;
    return std::labs((long)ia - (long)ib);
}

LegacyComparison legacy_difference(const FlatDesign& flat, const sw_flat::FlatDatabase& flat_db, const packer::Netlist& ours,
                                   const packer::Netlist& legacy, bool is_def)
{
    LegacyComparison result;
    auto fail = [&](const std::string& what) { result.difference = what; return result; };

    std::vector<int> legacy_id(flat.num_nodes(), -1);
    int next_id = 0;
    for (int node = 0; node < flat.num_nodes(); node++)
        if (flat.node_kind[node] != NodeKind::IO_PAD) legacy_id[node] = next_id++;
    for (int pin = 0; pin < (int)flat.pin_node.size(); pin++)
        if (legacy_id[flat.pin_node[pin]] < 0) legacy_id[flat.pin_node[pin]] = next_id++;
    if ((size_t)next_id != legacy.movable.size())
        return fail("legacy node count " + std::to_string(legacy.movable.size()) + " vs expected " + std::to_string(next_id));

    std::set<int> explained(flat_db.cover_nodes.begin(), flat_db.cover_nodes.end());
    explained.insert(flat_db.placed_fixed_nodes.begin(), flat_db.placed_fixed_nodes.end());
    explained.insert(flat_db.pl_fixed_nodes.begin(), flat_db.pl_fixed_nodes.end());
    std::set<int> differing;
    for (int node = 0; node < flat.num_nodes(); node++) {
        int id = legacy_id[node];
        if (id < 0) continue;
        if (ours.movable[node] != legacy.movable[id]) differing.insert(node);
        if (ours.movable[node] && legacy.movable[id]) {
            bool a = ours.is_macro[node], b = legacy.is_macro[id];
            result.macro_both += a && b; result.macro_only_flat += a && !b; result.macro_only_legacy += !a && b;
        }
    }
    if (differing != explained)
        return fail("movable differs on " + std::to_string(differing.size()) + " nodes, FlatDatabase explains " + std::to_string(explained.size()));

    if (ours.nets.size() != legacy.nets.size()) return fail("kept nets " + std::to_string(ours.nets.size()) + " vs " + std::to_string(legacy.nets.size()));
    const float scale = (float)flat.units_per_micron;
    for (size_t net = 0; net < ours.nets.size(); net++) {
        if (ours.nets[net].size() != legacy.nets[net].size()) return fail("kept net " + std::to_string(net) + " degree");
        for (size_t k = 0; k < ours.nets[net].size(); k++) {
            const packer::Pin& a = ours.nets[net][k];
            const packer::Pin& b = legacy.nets[net][k];
            if (legacy_id[a.node] != b.node) return fail("kept net " + std::to_string(net) + " pin " + std::to_string(k) + " node");
            for (int axis = 0; axis < 2; axis++) {
                float flat_offset = ours.offset_value[axis][a.offset_key[axis]];
                float legacy_offset = legacy.offset_value[axis][b.offset_key[axis]];
                float size = axis ? flat.node_height[a.node] : flat.node_width[a.node];
                float expected = is_def ? legacy_offset * scale : size / 2.0f + legacy_offset;
                if (same_bits(flat_offset, expected)) continue;
                if (is_def && ulps_apart(flat_offset, expected) <= 2) { result.centre_rounding_pins++; continue; }
                return fail("kept net " + std::to_string(net) + " pin " + std::to_string(k) + " axis " + std::to_string(axis)
                                + " offset " + hexf(flat_offset) + " vs legacy-converted " + hexf(expected));
            }
        }
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> dirs;
    bool with_legacy = true;
    for (int arg = 1; arg < argc; arg++) {
        if (std::string(argv[arg]) == "--no-legacy") with_legacy = false;
        else dirs.push_back(argv[arg]);
    }
    if (dirs.empty()) { std::cerr << "usage: flat_database_check [--no-legacy] <design_dir> [<design_dir> ...]\n"; return 2; }
    Logger::setup_logging(LogLevel::WARNING);
    int failed = 0;
    for (std::string dir : dirs) {
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        const std::string base = dir.substr(dir.find_last_of('/') + 1);
        const std::string suite = dir.substr(0, dir.find_last_of('/'));
        const std::string tag = suite.substr(suite.find_last_of('/') + 1) + "/" + base;

        auto start = std::chrono::steady_clock::now();
        sw_flat::FlatDatabase flat_db;
        std::string why;
        if (!sw_flat::read_flat_design(dir, flat_db, why)) { printf("FAIL %s: FlatDatabase: %s\n", tag.c_str(), why.c_str()); failed++; continue; }
        const FlatDesign& flat = flat_db.design;
        const double flat_seconds = seconds_since(start);
        start = std::chrono::steady_clock::now();
        DataBase db(dir);
        const double db_seconds = seconds_since(start);

        std::vector<std::string> failures;
        std::vector<Node*> db_node;
        std::string difference = database_difference(db, flat, db_node);
        if (!difference.empty()) failures.push_back("[database] " + difference);

        std::vector<char> movable_macro = sw_flat::tag_movable_macros(flat);
        int macros = 0;
        difference = macros_difference(db, flat, movable_macro, macros);
        if (!difference.empty()) failures.push_back("[macros] " + difference);

        const bool is_def = fs::exists(dir + "/floorplan.def");
        packer::Netlist ours = sw_flat::to_netlist(flat, movable_macro, tag);
        packer::Netlist legacy;
        LegacyComparison legacy_cmp;
        if (with_legacy) {
            legacy = is_def ? packer::read_def_native(dir + "/floorplan.def", base) : packer::read_bookshelf_native(dir, base);
            legacy_cmp = legacy_difference(flat, flat_db, ours, legacy, is_def);
            if (!legacy_cmp.difference.empty()) failures.push_back("[legacy] " + legacy_cmp.difference);
        }

        printf("%s %s: %d nodes, %d nets, %zu kept, %d movable macros; explained movable diffs: cover %zu placed->fixed %zu pl-fixed %zu\n",
               failures.empty() ? "PASS" : "FAIL", tag.c_str(), flat.num_nodes(), flat.num_nets(), ours.nets.size(), macros,
               flat_db.cover_nodes.size(), flat_db.placed_fixed_nodes.size(), flat_db.pl_fixed_nodes.size());
        if (with_legacy)
            printf("  [info] macro rule vs legacy (movable in both): both %ld, sw_only-only %ld, legacy-only %ld; "
                   "offset table x/y: flat %zu/%zu, legacy %zu/%zu; pin-axis offsets off by the legacy centre rounding %ld\n",
                   legacy_cmp.macro_both, legacy_cmp.macro_only_flat, legacy_cmp.macro_only_legacy,
                   ours.offset_value[0].size(), ours.offset_value[1].size(), legacy.offset_value[0].size(), legacy.offset_value[1].size(),
                   legacy_cmp.centre_rounding_pins);
        else
            printf("  [info] [legacy] not run (--no-legacy)\n");
        printf("  [info] parse seconds: FlatDatabase %.2f, DataBase %.2f\n", flat_seconds, db_seconds);
        for (const std::string& f : failures) printf("  %s\n", f.c_str());
        failed += !failures.empty();
        fflush(stdout);
    }
    printf("%s (%d of %zu designs failed)\n", failed ? "SOME FAILED" : "ALL PASS", failed, dirs.size());
    exit(failed ? 1 : 0);   // exit, not return: no teardown of millions of nodes. Meow.
}
