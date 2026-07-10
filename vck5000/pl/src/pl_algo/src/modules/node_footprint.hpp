#ifndef PL_ALGO_NODE_FOOTPRINT_HPP
#define PL_ALGO_NODE_FOOTPRINT_HPP

// node_footprint -- shared density footprint geometry for the density solve.
//
// Mirrors sw_only Grid::computeBinOverlaps (the software golden). When clamping, each cell
// is inflated to at least sqrt(2) bins per dimension and its deposited density is scaled by
// weight = real_area / clamped_area, so total area is conserved but a sub-bin cell is smeared
// across the grid resolution instead of spiking a single bin. The footprint is centered on the
// cell and shifted to stay on-grid so edge cells still deposit their full mass. Macros already
// exceed the clamp, so weight stays 1 and they are unchanged.
//
// This is the density FORCE smoothing (XPlace expand_ratio): the field solved from the
// smoothed rho -- and its adjoint force gather -- have no sub-bin gradient spikes, which is
// what stabilizes the optimizer and lowers HPWL. density_bin's scatter and force_gather's
// gather MUST use identical geometry (scatter deposits area; gather reads area*eField), so both
// call this one helper.

#include "../formats.hpp"
#include "../host_interface.hpp"

namespace plalgo {

// Density-clamp toggle. Compile-time so HLS constant-folds the branch; the software golden
// exposes the same behavior via the enable_density_clamp config knob.
constexpr bool ENABLE_DENSITY_CLAMP = true;

// Compute a node's density footprint [xl,xh) x [yl,yh) and the area-conserving weight.
static inline void node_footprint(const NodeBox& nd, float bin_w, float bin_h,
                                  float& xl, float& yl, float& xh, float& yh, float& weight) {
    const float w = nd.w, h = nd.h;
    float cw = w, ch = h;
    weight = 1.0f;
    if (ENABLE_DENSITY_CLAMP) {
        const float SQRT2 = 1.41421356f;
        const float min_w = bin_w * SQRT2, min_h = bin_h * SQRT2;
        cw = w > min_w ? w : min_w;                          // inflate sub-bin cells to ~grid res
        ch = h > min_h ? h : min_h;
        weight = (cw > 0.0f && ch > 0.0f) ? (w * h) / (cw * ch) : 0.0f;  // conserve total area
    }
    const float grid_w = GRID * bin_w, grid_h = GRID * bin_h;
    xl = nd.x + 0.5f * w - 0.5f * cw;                        // centered on the cell, then
    yl = nd.y + 0.5f * h - 0.5f * ch;
    if (xl + cw > grid_w) xl = grid_w - cw;                  // shifted to stay on-grid
    if (yl + ch > grid_h) yl = grid_h - ch;
    if (xl < 0.0f) xl = 0.0f;
    if (yl < 0.0f) yl = 0.0f;
    xh = xl + cw;
    yh = yl + ch;
}

} // namespace plalgo

#endif // PL_ALGO_NODE_FOOTPRINT_HPP
