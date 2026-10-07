#ifndef SW_FLAT_FLAT_NETLIST_HPP
#define SW_FLAT_FLAT_NETLIST_HPP

// flat_netlist.hpp -- what the packer is fed, derived from a FlatDesign by sw_only's rules
// (#45 step 3; Mark, 2026-10-07: the packer's macro rule IS sw_only's tagMovableMacros):
//   tag_movable_macros  sw_only's Placer::tagMovableMacros (Setup.cpp), the XPlace is_mov_macro rule
//   to_netlist          packer::Netlist in FlatDesign's node ids and DataBase's offset convention
//                       (DBU, from the node's lower-left), nets of degree 2..IGNORE_NET_DEGREE
// The legacy readers (vck5000/bring_up/beat_packer) had their own rules; test/flat_sink_check.cpp
// accounts for every difference. Meow.

#include "flat_design.hpp"
#include "packer/beat_packer.hpp"

#include <algorithm>
#include <numeric>

namespace sw_flat {

// Placer::tagMovableMacros over the components (IO pads are not components), in node order.
// height_sum is a float sum in node order; sw_only's runs in name order (its std::map), which can
// round differently -- flat_sink_check compares the two tag sets on every design. Meow.
inline std::vector<char> tag_movable_macros(const FlatDesign& design)
{
    std::vector<char> movable_macro(design.num_nodes(), 0);
    std::vector<float> movable_areas;
    float height_sum = 0.0f;
    for (int node = 0; node < design.num_nodes(); node++) {
        if (design.node_kind[node] != NodeKind::CELL) continue;
        movable_areas.push_back(design.node_width[node] * design.node_height[node]);
        height_sum += design.node_height[node];
    }
    if (movable_areas.empty()) return movable_macro;
    float row_height = height_sum / movable_areas.size();

    // Threshold from the smallest 99.9% of movable areas, so a handful of huge macros cannot drag
    // the mean up and hide themselves (Setup.cpp). Meow.
    std::sort(movable_areas.begin(), movable_areas.end());
    size_t small_count = std::max<size_t>(1, (size_t)(movable_areas.size() * 0.999));
    float mean_small_area = std::accumulate(movable_areas.begin(), movable_areas.begin() + small_count, 0.0f) / small_count;
    float macro_area_threshold = 10.0f * mean_small_area;

    for (int node = 0; node < design.num_nodes(); node++) {
        if (design.node_kind[node] != NodeKind::CELL) continue;
        float width = design.node_width[node], height = design.node_height[node];
        bool is_tall  = height > 2.01f * row_height;
        bool is_large = width * height > macro_area_threshold;
        bool is_sized = width > 1e-4f && height > 1e-4f;
        movable_macro[node] = is_tall && is_large && is_sized;
    }
    return movable_macro;
}

inline packer::Netlist to_netlist(const FlatDesign& design, const std::vector<char>& movable_macro,
                                  const std::string& name)
{
    packer::Netlist nl;
    nl.name = name;
    nl.movable.resize(design.num_nodes());
    for (int node = 0; node < design.num_nodes(); node++) nl.movable[node] = design.node_kind[node] == NodeKind::CELL;
    nl.is_macro = movable_macro;

    packer::OffsetDict dict[2];
    for (int net = 0; net < design.num_nets(); net++) {
        int degree = design.net_degree(net);
        if (degree < pinrec::MIN_NET_DEGREE || degree > packer::IGNORE_NET_DEGREE) continue;
        std::vector<packer::Pin>& pins = nl.nets.emplace_back(degree);
        for (int k = 0; k < degree; k++) {
            int pin = design.net_first_pin[net] + k;
            pins[k].node = design.pin_node[pin];
            pins[k].offset_key[0] = dict[0].key(design.pin_offset_x[pin], nl.offset_value[0]);
            pins[k].offset_key[1] = dict[1].key(design.pin_offset_y[pin], nl.offset_value[1]);
        }
    }
    return nl;
}

} // namespace sw_flat

#endif // SW_FLAT_FLAT_NETLIST_HPP
