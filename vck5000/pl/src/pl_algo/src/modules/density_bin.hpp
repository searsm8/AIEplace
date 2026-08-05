#ifndef PL_ALGO_DENSITY_BIN_HPP
#define PL_ALGO_DENSITY_BIN_HPP

// density_bin -- Stage 1 of the density solve: scatter node areas into the
// GRID x GRID bin-density grid rho (the ePlace charge density).
//
// Grid-strip tiled along x: a STRIP x GRID accumulator (256 KB) lives on-chip and
// nodes are re-streamed once per strip (~16x). Two-pass per strip, matching sw_only
// Density.cpp::computeOverlaps: fixed nodes first, clamp each bin to
// target_density*bin_area, then movable nodes on top. Per-node scatter mirrors
// Grid::computeBinOverlaps (fast path for sub-bin cells, else exact rect
// intersection). Fillers EXCLUDED in v1. rho = clamped_overlap / bin_area, written
// x-major (bin_density[x*GRID+y]) per host_interface.hpp.
//
// Algorithm verified bit-exact vs a naive full-grid scatter in
// model/density_bin_model.cpp; verified vs the Grid golden (real benchmark) in sw_emu.
//
// NOTE: acc_URAM[][] += in bin_scatter is a float-accumulator RMW (same hazard class
// as hpwl_CU's scatter); the node-loop II is measured in C-synth before optimizing.
// (DDR traffic is benign: node_box read is sequential->burst, bin_density write is
//  sequential->burst and write-only -- the accumulation RMW stays on-chip in URAM.)

#include "../formats.hpp"
#include "../host_interface.hpp"
#include "node_footprint.hpp"

namespace plalgo {

constexpr int STRIP = 64;                  // x-values per strip; GRID/STRIP = 16 strips

// Scatter one node's (area-conserving, clamped) density into the strip accumulator
// acc_URAM[STRIP][GRID], for the x-bins (columns) in [c0, c0+STRIP). The footprint geometry
// comes from node_footprint (shared with force_gather); a sub-bin cell is smeared to ~grid
// resolution with weight = real_area/clamped_area, so no bin spikes. Mirrors the software
// golden Grid::computeBinOverlaps with clamping enabled.
static void bin_scatter(const NodeBox& nd, float bin_w, float bin_h, int c0,
                        float acc_URAM[STRIP][GRID]) {
    float xl, yl, xh, yh, weight;
    node_footprint(nd, bin_w, bin_h, xl, yl, xh, yh, weight);
    // Bin-index range the footprint spans, clamped to the grid (col = x-bin, row = y-bin).
    int col_lo = (int)(xl / bin_w);  if (col_lo < 0)        col_lo = 0;   // first x-bin touched
    int col_hi = (int)(xh / bin_w);  if (col_hi > GRID - 1) col_hi = GRID - 1; // last x-bin touched
    int row_lo = (int)(yl / bin_h);  if (row_lo < 0)        row_lo = 0;   // first y-bin touched
    int row_hi = (int)(yh / bin_h);  if (row_hi > GRID - 1) row_hi = GRID - 1; // last y-bin touched

    const int c1 = c0 + STRIP;
    // Exact rectangle intersection per covered bin, clipped to the strip's columns.
    const int cs = col_lo > c0      ? col_lo : c0;       // first x-bin in this strip
    const int ce = col_hi < c1 - 1  ? col_hi : c1 - 1;   // last x-bin in this strip
    for (int col = cs; col <= ce; col++) {
        const float lx = col * bin_w, rx = lx + bin_w;   // this bin's x-extent [lx, rx)
        // overlap width = intersection of footprint [xl,xh] with bin [lx,rx]
        const float ox = (xh < rx ? xh : rx) - (xl > lx ? xl : lx);
        if (ox <= 0) continue;                           // footprint doesn't reach this column
        for (int row = row_lo; row <= row_hi; row++) {
            const float ly = row * bin_h, ry = ly + bin_h; // this bin's y-extent [ly, ry)
            // overlap height = intersection of footprint [yl,yh] with bin [ly,ry]
            const float oy = (yh < ry ? yh : ry) - (yl > ly ? yl : ly);
            if (oy <= 0) continue;                       // footprint doesn't reach this row
            acc_URAM[col - c0][row] += ox * oy * weight; // area-conserving density deposit
        }
    }
}

static void density_bin(const NodeBox* node_box,    // [num_nodes]  movable [0,M), fixed [M,N)
                        float*         bin_density,  // [GRID*GRID]  rho, x-major rho[x*GRID+y]
                        int            num_movable,
                        int            num_nodes,
                        float          bin_w,
                        float          bin_h,
                        float          target_density) {
    static float acc_URAM[STRIP][GRID];                  // on-chip strip accumulator (256 KB)
#pragma HLS bind_storage variable=acc_URAM type=RAM_2P impl=URAM
    const float bin_area = bin_w * bin_h;
    const float cap      = target_density * bin_area;    // per-bin fixed-density clamp
    const float inv_area = 1.0f / bin_area;

strip_loop:
    for (int c0 = 0; c0 < GRID; c0 += STRIP) {
    clear_i:
        for (int i = 0; i < STRIP; i++)
        clear_y:
            for (int y = 0; y < GRID; y++) {
#pragma HLS PIPELINE II=1
                acc_URAM[i][y] = 0.0f;
            }

    pass1_fixed:
        for (int n = num_movable; n < num_nodes; n++)
            bin_scatter(node_box[n], bin_w, bin_h, c0, acc_URAM);

    // Clamp all fixed nodes (large macros) to target_density*bin_area before movable nodes are added.
    // This ensures no overflow occurs unless a movable node is on top of a fixed node.
    clamp_i:
        for (int i = 0; i < STRIP; i++)
        clamp_y:
            for (int y = 0; y < GRID; y++) {
#pragma HLS PIPELINE II=1
                if (acc_URAM[i][y] > cap) acc_URAM[i][y] = cap;
            }

    pass2_movable:
        for (int n = 0; n < num_movable; n++)
            bin_scatter(node_box[n], bin_w, bin_h, c0, acc_URAM);

    write_i:
        for (int i = 0; i < STRIP; i++)
        write_y:
            for (int y = 0; y < GRID; y++) {
#pragma HLS PIPELINE II=1
                bin_density[(c0 + i) * GRID + y] = acc_URAM[i][y] * inv_area;
            }
    }
}

} // namespace plalgo

#endif // PL_ALGO_DENSITY_BIN_HPP
