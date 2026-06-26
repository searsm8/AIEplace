// top.cpp -- pl_algo PL kernel.
//
// HPWL gradient on the PL (no AIE). Two-phase, arbitrary-size schedule (see
// hpwl_CU.hpp): phase A computes per-net bbox/sums net-major (net-tiled) and spills
// them to DDR (bb_DDR/sums_DDR); phase B streams pins node-major (npins) as a
// segmented reduction, writing node_grad once per node. Verified against markv1
// computeHpwlPartials_CPU on a real benchmark.
//
// The AIE HPWL-gradient graph is parked in the tree (aie/src/pl_algo) but unused.
//
// Natural typed m_axi pointers; 128-bit beat packing (formats.hpp) is a later
// throughput optimization.

#include "host_interface.hpp"
#include "modules/hpwl_CU.hpp"

using namespace plalgo;

extern "C" {
void top(
    const coord_t* node_pos,     // [num_nodes]   movable [0,M), fixed [M,N)
    const int*     net_ptr,      // [num_nets+1]  CSR prefix offsets into pins
    const NodePin* pins,         // [num_pins]    flattened, NET-major
    const NodePin* npins,        // [num_npins]   movable pins, NODE-major (sorted)
    const float*   exp_lut,      // [lut_size]    normalized exp(-t) table
    NetBBox*       bb,           // [num_nets]    scratch (phase A -> phase B)
    NetSums*       sums,         // [num_nets]    scratch (phase A -> phase B)
    coord_t*       node_grad,    // [num_movable] HPWL gradient (output)
    float          inv_gamma,    // 1/gamma for the WA formula
    float          inv_lut_step, // LUT index scale (gamma-dependent)
    int            lut_size,
    int            num_nets,
    int            num_movable,
    int            num_npins)
{
#pragma HLS INTERFACE m_axi port=node_pos  offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=net_ptr   offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=pins      offset=slave bundle=gmem2
#pragma HLS INTERFACE m_axi port=npins     offset=slave bundle=gmem3
#pragma HLS INTERFACE m_axi port=exp_lut   offset=slave bundle=gmem4
#pragma HLS INTERFACE m_axi port=bb        offset=slave bundle=gmem5
#pragma HLS INTERFACE m_axi port=sums      offset=slave bundle=gmem6
#pragma HLS INTERFACE m_axi port=node_grad offset=slave bundle=gmem7
// Each m_axi port's offset register and every scalar arg share the one AXI-Lite
// "control" bundle in Vitis kernel mode.
#pragma HLS INTERFACE s_axilite port=node_pos     bundle=control
#pragma HLS INTERFACE s_axilite port=net_ptr      bundle=control
#pragma HLS INTERFACE s_axilite port=pins         bundle=control
#pragma HLS INTERFACE s_axilite port=npins        bundle=control
#pragma HLS INTERFACE s_axilite port=exp_lut      bundle=control
#pragma HLS INTERFACE s_axilite port=bb           bundle=control
#pragma HLS INTERFACE s_axilite port=sums         bundle=control
#pragma HLS INTERFACE s_axilite port=node_grad    bundle=control
#pragma HLS INTERFACE s_axilite port=inv_gamma    bundle=control
#pragma HLS INTERFACE s_axilite port=inv_lut_step bundle=control
#pragma HLS INTERFACE s_axilite port=lut_size     bundle=control
#pragma HLS INTERFACE s_axilite port=num_nets     bundle=control
#pragma HLS INTERFACE s_axilite port=num_movable  bundle=control
#pragma HLS INTERFACE s_axilite port=num_npins    bundle=control
#pragma HLS INTERFACE s_axilite port=return       bundle=control

    hpwl_CU(node_pos, net_ptr, pins, npins, exp_lut, bb, sums, node_grad,
            inv_gamma, inv_lut_step, lut_size, num_nets, num_movable, num_npins);
}
} // extern "C"
