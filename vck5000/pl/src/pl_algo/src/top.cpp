// top.cpp -- pl_algo PL kernel.
//
// HPWL gradient on the PL (no AIE): the hpwl_CU module gathers pin coords from
// DDR, reduces per net, applies a LUT exp, and scatter-accumulates the
// weighted-average HPWL gradient (dW/dx, dW/dy) onto the movable nodes. Verified
// against markv1 computeHpwlPartials_CPU on a real benchmark.
//
// This is the single top-level kernel of the PL-centric design; the other modules
// (memory_writer / density_manager / iteration_update / metrics) and the AIE FFT
// pool are wired in incrementally. The AIE HPWL-gradient graph is parked in the
// tree (aie/src/pl_algo) but unused -- HPWL moved fully onto the PL.
//
// Natural typed m_axi pointers (coord_t / PinRecord / int / float) for clarity;
// 128-bit beat packing (formats.hpp) is a later throughput optimization.

#include "host_interface.hpp"
#include "modules/hpwl_CU.hpp"

using namespace plalgo;

extern "C" {
void top(
    const coord_t*   node_pos,     // [num_nodes]   movable [0,M), fixed [M,N)
    const int*       net_ptr,      // [num_nets+1]  CSR prefix offsets into pins
    const PinRecord* pins,         // [num_pins]    flattened, net-major
    const float*     exp_lut,      // [lut_size]    normalized exp(-t) table
    coord_t*         node_grad,    // [num_movable] HPWL gradient (output)
    float            inv_gamma,    // 1/gamma for the WA formula
    float            inv_lut_step, // LUT index scale (gamma-dependent)
    int              lut_size,
    int              num_nets,
    int              num_movable)
{
#pragma HLS INTERFACE m_axi port=node_pos  offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=net_ptr   offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=pins      offset=slave bundle=gmem2
#pragma HLS INTERFACE m_axi port=exp_lut   offset=slave bundle=gmem3
#pragma HLS INTERFACE m_axi port=node_grad offset=slave bundle=gmem4
// Each m_axi port's offset register and every scalar arg must share the one
// AXI-Lite "control" bundle in Vitis kernel mode.
#pragma HLS INTERFACE s_axilite port=node_pos     bundle=control
#pragma HLS INTERFACE s_axilite port=net_ptr      bundle=control
#pragma HLS INTERFACE s_axilite port=pins         bundle=control
#pragma HLS INTERFACE s_axilite port=exp_lut      bundle=control
#pragma HLS INTERFACE s_axilite port=node_grad    bundle=control
#pragma HLS INTERFACE s_axilite port=inv_gamma    bundle=control
#pragma HLS INTERFACE s_axilite port=inv_lut_step bundle=control
#pragma HLS INTERFACE s_axilite port=lut_size     bundle=control
#pragma HLS INTERFACE s_axilite port=num_nets     bundle=control
#pragma HLS INTERFACE s_axilite port=num_movable  bundle=control
#pragma HLS INTERFACE s_axilite port=return       bundle=control

    hpwl_CU(node_pos, net_ptr, pins, exp_lut, node_grad,
            inv_gamma, inv_lut_step, lut_size, num_nets, num_movable);
}
} // extern "C"
