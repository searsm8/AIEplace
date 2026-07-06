#ifndef PL_ALGO_FORCE_GATHER_HPP
#define PL_ALGO_FORCE_GATHER_HPP

// force_gather -- Stage 5: the electrostatic force gather, i.e. the per-node density
// gradient. For each movable node, sum the E-field over the bins the node overlaps,
// weighting EACH bin by the node-bin overlap area:
//     grad(node) = sum_bins overlap_area(node,bin) * eField(bin)
// This is the ADJOINT of density_bin's area scatter (density_bin scatters overlap_area
// INTO rho; this gathers overlap_area * eField OUT). The overlap area is required, not
// optional -- confirmed vs DREAMPlace electric_force_cuda_kernel (area * field_map) and
// Xplace density_map_cuda_backward (overlap_area * grad_mat); markv1's original
// computeElectrostaticForce omitted it (fixed 2026-07-03).
//
// Overlaps are REGENERATED here (same rectangle-intersection geometry as
// density_bin.hpp bin_scatter) rather than stored: density_bin keeps only the summed rho,
// so the per-node overlap breakdown the force needs no longer exists. Nodes are
// re-streamed against the two DDR-resident field matrices (Ex, Ey).
//
// The density_weight (lambda schedule) and the constant local_density_weight are scalar
// multipliers applied downstream at the combine step (lambda stays on the host), so this
// module emits the pure overlap-area-weighted field sum. Movable nodes only ([0,M)); fixed
// nodes carry no gradient.
//
// v1 is a straightforward per-node gather (each node reads its small bin rectangle from the
// field matrices in DDR -- a scattered read). Strip-tiling the field on-chip (the density_bin
// mirror) is a later bandwidth optimization, not a correctness concern.

#include "../formats.hpp"
#include "../host_interface.hpp"
#include "node_footprint.hpp"

namespace plalgo {

// Gather one node's force: field summed over its (clamped, area-conserving) footprint bins,
// weighted by the deposited overlap. This is the exact adjoint of bin_scatter, so it uses the
// SAME node_footprint geometry -- the same clamped footprint and area-conserving weight.
// eField_x / eField_y are GRID x GRID, x-major (idx = col*GRID + row), matching bin_density.
static void node_gather(const NodeBox& nd, const float* eField_x, const float* eField_y,
                        float bin_w, float bin_h, float& grad_x, float& grad_y) {
    float xl, yl, xh, yh, weight;
    node_footprint(nd, bin_w, bin_h, xl, yl, xh, yh, weight);
    int col_lo = (int)(xl / bin_w);  if (col_lo < 0)        col_lo = 0;   // first x-bin touched
    int col_hi = (int)(xh / bin_w);  if (col_hi > GRID - 1) col_hi = GRID - 1; // last x-bin touched
    int row_lo = (int)(yl / bin_h);  if (row_lo < 0)        row_lo = 0;   // first y-bin touched
    int row_hi = (int)(yh / bin_h);  if (row_hi > GRID - 1) row_hi = GRID - 1; // last y-bin touched

    float acc_x = 0.0f, acc_y = 0.0f;
    // Exact rectangle intersection per covered bin (mirrors bin_scatter's deposit).
    for (int col = col_lo; col <= col_hi; col++) {
        const float lx = col * bin_w, rx = lx + bin_w;   // this bin's x-extent [lx, rx)
        const float ox = (xh < rx ? xh : rx) - (xl > lx ? xl : lx);   // x-overlap width
        if (ox <= 0) continue;
        for (int row = row_lo; row <= row_hi; row++) {
            const float ly = row * bin_h, ry = ly + bin_h; // this bin's y-extent [ly, ry)
            const float oy = (yh < ry ? yh : ry) - (yl > ly ? yl : ly);   // y-overlap height
            if (oy <= 0) continue;
            const float area = ox * oy * weight;         // area-conserving deposit (matches scatter)
            const int   idx  = col * GRID + row;
            acc_x += area * eField_x[idx];
            acc_y += area * eField_y[idx];
        }
    }
    grad_x = acc_x;
    grad_y = acc_y;
}

static void force_gather(const NodeBox* node_box,    // [num_nodes]  movable [0,M), fixed [M,N)
                         const float*   eField_x,    // [GRID*GRID]  x-major eField.x
                         const float*   eField_y,    // [GRID*GRID]  x-major eField.y
                         coord_t*       node_grad,    // [num_movable] density gradient out
                         int            num_movable,
                         float          bin_w,
                         float          bin_h) {
node_loop:
    for (int n = 0; n < num_movable; n++) {
        float grad_x, grad_y;
        node_gather(node_box[n], eField_x, eField_y, bin_w, bin_h, grad_x, grad_y);
        node_grad[n].x = grad_x;
        node_grad[n].y = grad_y;
    }
}

} // namespace plalgo

#endif // PL_ALGO_FORCE_GATHER_HPP
