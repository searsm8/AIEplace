#ifndef SW_FLAT_FILLERS_HPP
#define SW_FLAT_FILLERS_HPP

// fillers.hpp -- filler count and size on a FlatDesign: sw_only's DataBase::addFillers
// (host/src/common/src/DataBase.cpp), itself XPlace's filler sizing (database.py). Fillers are
// anonymous, identical and netless, so a count and one size describe them all; they sit in the
// density-only tail of the slot layout (slot_layout.hpp). Their positions are the placer's (step 5).
//
// Sums run in NAME order because DataBase's do (its components are a std::map): the float sum of
// movable-macro area rounds differently in another order, and test/slot_layout_check.cpp holds
// count, size and effective target density bit-equal to DataBase's. Meow.

#include "flat_design.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace sw_flat {

struct Fillers {
    int   count = 0;
    float width = 0, height = 0;
    float target_density = 0;   // effective: raised to the standard-cell utilization when that is higher
};

// Placer::readConfig then Setup: the benchmark's placement.constraints overrides the config. Meow.
inline float effective_target_density(const FlatDesign& design, float configured)
{
    return design.maximum_utilization > 0.0f ? design.maximum_utilization : configured;
}

inline Fillers make_fillers(const FlatDesign& design, const std::vector<char>& movable_macro, float target_density)
{
    std::vector<int> by_name;
    for (int node = 0; node < design.num_nodes(); node++)
        if (design.node_kind[node] != NodeKind::IO_PAD) by_name.push_back(node);   // IO pads are not components
    std::sort(by_name.begin(), by_name.end(), [&](int a, int b) { return design.node_name[a] < design.node_name[b]; });

    // DataBase::computeAreaBreakdown: fixed area clipped to the die, double sums cast to float. Meow.
    double movable_sum = 0, fixed_sum = 0;
    float movable_macro_area = 0.0f;
    std::vector<float> stdcell_widths, stdcell_heights;
    for (int node : by_name) {
        const float x = design.node_x[node], y = design.node_y[node];
        const float width = design.node_width[node], height = design.node_height[node];
        if (design.node_kind[node] == NodeKind::FIXED) {
            float overlap_x = std::max(0.0f, std::min(x + width, design.die_xh) - std::max(x, design.die_xl));
            float overlap_y = std::max(0.0f, std::min(y + height, design.die_yh) - std::max(y, design.die_yl));
            fixed_sum += (double)overlap_x * overlap_y;
            continue;
        }
        movable_sum += width * height;
        if (movable_macro[node]) { movable_macro_area += width * height; continue; }
        stdcell_widths.push_back(width);
        stdcell_heights.push_back(height);
    }
    Fillers fillers;
    fillers.target_density = target_density;
    if (stdcell_widths.empty()) return fillers;

    auto trimmed_mean = [](std::vector<float>& sizes) {   // drop the smallest and largest 5%
        std::sort(sizes.begin(), sizes.end());
        int n = (int)sizes.size();
        int lo = (int)(n * 0.05f), hi = (int)(n * 0.95f);
        if (hi <= lo) { lo = 0; hi = n; }
        double sum = 0.0;
        for (int i = lo; i < hi; i++) sum += sizes[i];
        return (float)(sum / (hi - lo));
    };
    fillers.width  = trimmed_mean(stdcell_widths);
    fillers.height = design.row_height > 0.0f ? design.row_height : trimmed_mean(stdcell_heights);

    const float die_area               = (design.die_xh - design.die_xl) * (design.die_yh - design.die_yl);
    const float stdcell_placeable_area = die_area - (float)fixed_sum - movable_macro_area;
    const float stdcell_area           = (float)movable_sum - movable_macro_area;
    const float stdcell_utilization    = stdcell_area / std::max(1.0f, stdcell_placeable_area);
    if (stdcell_utilization > fillers.target_density) fillers.target_density = stdcell_utilization;

    const float whitespace_area = std::max(0.0f, fillers.target_density * stdcell_placeable_area - stdcell_area);
    fillers.count = (int)std::lround(whitespace_area / (fillers.width * fillers.height));
    return fillers;
}

} // namespace sw_flat

#endif // SW_FLAT_FILLERS_HPP
