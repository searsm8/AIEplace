// top.cpp -- pl_algo PL kernel.
//
// One kernel, two modules selected by `mode` (host_interface.hpp top_mode). This
// is a bring-up scaffold: each module is verified independently through one
// kernel while we build toward the unified per-iteration datapath (Stage 5).
//   MODE_HPWL_GRAD   -> hpwl_CU   : HPWL gradient (two-phase segmented reduction,
//                                   verified vs markv1 computeHpwlPartials_CPU).
//   MODE_DENSITY_BIN -> density_bin: bin density rho (strip-tiled scatter, verified
//                                   vs markv1 computeOverlaps/getBinDensities).
// The inactive module's buffers are still kernel args (the host binds 1-element
// dummies); each module only touches its own buffers, so the dummies are inert.
//
// The AIE HPWL-gradient graph is parked in the tree (aie/src/pl_algo) but unused.
//
// Natural typed m_axi pointers; 128-bit beat packing (formats.hpp) is a later
// throughput optimization.

#include "host_interface.hpp"
#include "modules/hpwl_CU.hpp"
#include "modules/density_bin.hpp"

using namespace plalgo;

extern "C" {
void top(
    // ---- HPWL gradient buffers (group_id 0-7) ----
    const coord_t* node_pos,     // [num_nodes]   movable [0,M), fixed [M,N)
    const int*     net_ptr,      // [num_nets+1]  CSR prefix offsets into pins
    const NodePin* pins,         // [num_pins]    flattened, NET-major
    const NodePin* npins,        // [num_npins]   movable pins, NODE-major (sorted)
    const float*   exp_lut,      // [lut_size]    normalized exp(-t) table
    NetBBox*       bb,           // [num_nets]    scratch (phase A -> phase B)
    NetSums*       sums,         // [num_nets]    scratch (phase A -> phase B)
    coord_t*       node_grad,    // [num_movable] HPWL gradient (output)
    // ---- density buffers (group_id 8-9) ----
    const NodeBox* node_box,     // [num_nodes]   {x,y,w,h} for binning
    float*         bin_density,  // [GRID*GRID]   rho output, x-major rho[x*GRID+y]
    // ---- HPWL scalars ----
    float          inv_gamma,    // 1/gamma for the WA formula
    float          inv_lut_step, // LUT index scale (gamma-dependent)
    int            lut_size,
    int            num_nets,
    int            num_movable,
    int            num_npins,
    // ---- density scalars ----
    int            num_nodes,    // N (movable + fixed)
    float          bin_w,
    float          bin_h,
    float          target_density,
    // ---- mode selector ----
    int            mode)
{
#pragma HLS INTERFACE m_axi port=node_pos    offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=net_ptr     offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=pins        offset=slave bundle=gmem2
#pragma HLS INTERFACE m_axi port=npins       offset=slave bundle=gmem3
#pragma HLS INTERFACE m_axi port=exp_lut     offset=slave bundle=gmem4
#pragma HLS INTERFACE m_axi port=bb          offset=slave bundle=gmem5
#pragma HLS INTERFACE m_axi port=sums        offset=slave bundle=gmem6
#pragma HLS INTERFACE m_axi port=node_grad   offset=slave bundle=gmem7
#pragma HLS INTERFACE m_axi port=node_box    offset=slave bundle=gmem8
#pragma HLS INTERFACE m_axi port=bin_density offset=slave bundle=gmem9
// Each m_axi port's offset register and every scalar arg share the one AXI-Lite
// "control" bundle in Vitis kernel mode.
#pragma HLS INTERFACE s_axilite port=node_pos       bundle=control
#pragma HLS INTERFACE s_axilite port=net_ptr        bundle=control
#pragma HLS INTERFACE s_axilite port=pins           bundle=control
#pragma HLS INTERFACE s_axilite port=npins          bundle=control
#pragma HLS INTERFACE s_axilite port=exp_lut        bundle=control
#pragma HLS INTERFACE s_axilite port=bb             bundle=control
#pragma HLS INTERFACE s_axilite port=sums           bundle=control
#pragma HLS INTERFACE s_axilite port=node_grad      bundle=control
#pragma HLS INTERFACE s_axilite port=node_box       bundle=control
#pragma HLS INTERFACE s_axilite port=bin_density    bundle=control
#pragma HLS INTERFACE s_axilite port=inv_gamma      bundle=control
#pragma HLS INTERFACE s_axilite port=inv_lut_step   bundle=control
#pragma HLS INTERFACE s_axilite port=lut_size       bundle=control
#pragma HLS INTERFACE s_axilite port=num_nets       bundle=control
#pragma HLS INTERFACE s_axilite port=num_movable    bundle=control
#pragma HLS INTERFACE s_axilite port=num_npins      bundle=control
#pragma HLS INTERFACE s_axilite port=num_nodes      bundle=control
#pragma HLS INTERFACE s_axilite port=bin_w          bundle=control
#pragma HLS INTERFACE s_axilite port=bin_h          bundle=control
#pragma HLS INTERFACE s_axilite port=target_density bundle=control
#pragma HLS INTERFACE s_axilite port=mode           bundle=control
#pragma HLS INTERFACE s_axilite port=return         bundle=control

    if (mode == MODE_DENSITY_BIN) {
        density_bin(node_box, bin_density, num_movable, num_nodes,
                    bin_w, bin_h, target_density);
    } else { // MODE_HPWL_GRAD
        hpwl_CU(node_pos, net_ptr, pins, npins, exp_lut, bb, sums, node_grad,
                inv_gamma, inv_lut_step, lut_size, num_nets, num_movable, num_npins);
    }
}
} // extern "C"
