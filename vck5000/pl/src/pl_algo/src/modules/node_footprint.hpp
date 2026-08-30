#ifndef PL_ALGO_NODE_FOOTPRINT_HPP
#define PL_ALGO_NODE_FOOTPRINT_HPP

// node_footprint -- shared density footprint geometry for the density solve.
//
// Mirrors sw_only computeNodeFootprint (Grid.cpp:9, the software golden). When clamping, each
// cell is inflated to at least sqrt(2) bins per dimension and its deposited density is scaled by
// weight = real_area / clamped_area, so total area is conserved but a sub-bin cell is smeared
// across the grid resolution instead of spiking a single bin. The footprint is CENTERED on the
// cell, with NO in-die shift: legality is guaranteed upstream by iteration_update's expanded-box
// position clamp (the same sqrt(2)-expanded size), exactly as sw_only relies on
// enforceDieBoundaries (Step.cpp:131) -- so the deposited mass stays centred on the cell it
// belongs to and needs no deposit-time correction (TODO #11a, #20 step 4). Macros already exceed
// the clamp, so weight stays 1 and they are unchanged.
//
// This is the density FORCE smoothing (XPlace expand_ratio): the field solved from the
// smoothed rho -- and its adjoint force gather -- have no sub-bin gradient spikes, which is
// what stabilizes the optimizer and lowers HPWL. density_bin's scatter and force_gather's
// gather MUST use identical geometry (scatter deposits area; gather reads area*eField), so both
// call this one helper -- which is also why the MOVABLE-MACRO deposit-weight override lives here
// and nowhere else: putting it in the shared helper keeps the scatter and gather exact adjoints
// for free (TODO #11b). For a movable macro with target_density < 1, XPlace OVERWRITES the
// area-conserving ratio with target_density (Grid.cpp:31, database.py:921-923); the caller passes
// is_movable_macro (derived from the node's index range, host_interface.hpp classifyNode).

#include "../formats.hpp"
#include "../host_interface.hpp"

namespace plalgo {

// Density-clamp toggle. Compile-time so HLS constant-folds the branch; the software golden
// exposes the same behavior via the enable_density_clamp config knob.
constexpr bool ENABLE_DENSITY_CLAMP = true;

// Compute a node's density footprint [xl,xh) x [yl,yh) and the area-conserving weight.
// is_movable_macro + target_density drive the #11b macro deposit-weight override (see the header);
// pass is_movable_macro=false to get the plain area-conserving weight (std-cells, fillers, fixed).
static inline void node_footprint(const NodeBox& nd, float bin_w, float bin_h,
                                  bool is_movable_macro, float target_density,
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
        // #11b: a movable macro at target_density<1 deposits at target_density, REPLACING (not
        // scaling) the ratio (Grid.cpp:31). Inside the clamp branch on purpose -- part of the
        // smoothed density model, exactly as computeNodeFootprint. Meow.
        if (target_density < 1.0f && is_movable_macro) weight = target_density;
    }
    // Centered on the cell; NO in-die shift (matches computeNodeFootprint). The upstream expanded
    // clamp keeps this box in-die; a footprint that still reaches past the grid is clipped by the
    // caller's bin-range intersection (fixed nodes), never translated. Meow.
    xl = nd.x + 0.5f * w - 0.5f * cw;
    yl = nd.y + 0.5f * h - 0.5f * ch;
    xh = xl + cw;
    yh = yl + ch;
}

} // namespace plalgo

#endif // PL_ALGO_NODE_FOOTPRINT_HPP
