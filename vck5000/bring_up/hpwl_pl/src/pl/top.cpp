// top.cpp -- standalone PL hardware harness for pl_algo's HPWL gradient compute unit
// (vck5000/pl/src/pl_algo/src/modules/hpwl_gradient.hpp: plalgo::hpwl_CU), on the PL with
// NO AIE. This is the module MODE_HPWL_GRAD in pl_algo/top.cpp dispatches to; here it gets
// its own dedicated kernel with only the HPWL buffers as args (no inert dummy ports for the
// density/DCT buffers that mode never touches) -- same framework as ../../fft_pl,
// ../../field_solve_pl and ../../add1_pl.
//
// The module is included directly (-I, see ../../Makefile), not copied, so this harness
// always tests the actual hpwl_CU, not a fork of it.
//
// Eight m_axi ports (DDR), no scalars beyond the HPWL params, no AIE graph, no AXIS stream
// ports, no link.cfg connectivity. bb/sums are device-only scratch (num_nets each): the host
// allocates them but neither fills nor reads them -- hpwl_CU uses them to bridge its three
// segmented-reduction passes (see hpwl_gradient.hpp's header comment for the CSR/SpMV
// analogy). All arrays are arbitrary-size (DDR-resident), unlike field_solve_pl's on-chip
// fixed grid -- this module's whole point is no size cap.

#include "modules/hpwl_gradient.hpp"

using namespace plalgo;

extern "C" {
void hpwl_top(const coord_t* node_pos,   // [num_nodes] AoS {x,y}, movable first
              const int*     net_ptr,    // [num_nets+1] CSR (net_ptr[num_nets] == num_pins)
              const NodePin* pins,       // [num_pins] NET-major (CSR order)
              const NodePin* npins,      // [num_npins] NODE-major (sorted), movable+gradient-bearing only
              const float*   exp_lut,    // [lut_size] exp(-t) table, t = d/gamma normalized
              NetBBox*       bb,         // [num_nets] scratch (kernel writes then reads)
              NetSums*       sums,       // [num_nets] scratch (kernel writes then reads)
              coord_t*       node_grad,  // [num_movable] gradient (output)
              float          inv_gamma,
              float          inv_lut_step,
              int            lut_size,
              int            num_nets,
              int            num_movable,
              int            num_npins) {
#pragma HLS INTERFACE m_axi port=node_pos  offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=net_ptr   offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=pins      offset=slave bundle=gmem2
#pragma HLS INTERFACE m_axi port=npins     offset=slave bundle=gmem3
#pragma HLS INTERFACE m_axi port=exp_lut   offset=slave bundle=gmem4
#pragma HLS INTERFACE m_axi port=bb        offset=slave bundle=gmem5
#pragma HLS INTERFACE m_axi port=sums      offset=slave bundle=gmem6
#pragma HLS INTERFACE m_axi port=node_grad offset=slave bundle=gmem7
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
