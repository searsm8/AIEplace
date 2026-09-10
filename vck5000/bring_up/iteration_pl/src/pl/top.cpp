// top.cpp -- standalone PL hardware harness for ONE full pl_algo Nesterov placement step:
// both gradient sources (HPWL + density) computed and combined into a position update, all
// on the PL, still with NO AIE. This is the next rung above ../hpwl_pl (HPWL gradient alone)
// and ../field_solve_pl (density field solve alone) -- it chains FIVE already-proven modules
// into one kernel:
//
//   hpwl_CU        node_pos, net connectivity            -> g_hpwl[M]     (../../hpwl_pl)
//   density_bin    node_box (scatter)                    -> rho[GRID*GRID]
//   field_solve_pl rho (forward 2D DCT -> spectral -> inverse) -> Ex,Ey   (../../field_solve_pl)
//   force_gather   node_box, Ex, Ey                       -> g_density[M]
//   iteration_update + memory_writer
//                  g_hpwl, g_density, v_k, u_k, precond   -> u_{k+1}, v_{k+1}
//
// This is deliberately NOT the device-resident Stage 5 loop (DATAFLOW.md) -- no on-chip
// schedule/convergence, no looping, lambda/alpha/coeff are host-supplied scalars for a single
// step (matching pl_algo's v1 host-owned policy). It is the "--one-iter" sw_emu bring-up mode
// (main.cpp) with the HPWL gradient wired in for real (that mode currently runs g_hpwl=0,
// density-only), built standalone for real silicon like every other harness in this ladder.
//
// All five modules are included directly (-I, see ../../Makefile), not copied. Small-grid
// build: PL_GRID (default 64, matching ../../field_solve_pl) sizes the on-chip density
// matrices; ITER_MAX_NODES (default 128) bounds the on-chip node_pos/gradient scratch that
// hpwl_CU and force_gather need extracted from node_box. net/pin arrays stay arbitrary-size
// DDR (matching ../../hpwl_pl) since hpwl_CU's whole point is no size cap there.

#ifndef PL_GRID
#define PL_GRID 64
#endif
#ifndef ITER_MAX_NODES
#define ITER_MAX_NODES 128
#endif

#include "modules/hpwl_gradient.hpp"
#include "modules/density_bin.hpp"
#include "modules/field_solve_pl.hpp"
#include "modules/force_gather.hpp"
#include "modules/iteration_update.hpp"
#include "modules/memory_writer.hpp"

using namespace plalgo;

// Stage 5c DATAFLOW pair (iteration_update producer -> stream -> memory_writer consumer),
// same shape as pl_algo/top.cpp's iteration_step_df.
static void iteration_step_df(const coord_t* g_hpwl, const coord_t* g_density,
                              const NodeBox* node_box, const coord_t* u_in,
                              const float* precond, coord_t* u_out, coord_t* v_out,
                              float lambda, float alpha, float coeff,
                              float die_xmax, float die_ymax, int num_movable) {
#pragma HLS DATAFLOW
    hls::stream<coord_t> v_edge;
#pragma HLS STREAM variable=v_edge depth=64
    iteration_update(g_hpwl, g_density, node_box, u_in, precond, u_out,
                     lambda, alpha, coeff, die_xmax, die_ymax, num_movable, v_edge);
    memory_writer(v_out, v_edge, num_movable);
}

extern "C" {
void iteration_top(
    // ---- HPWL gradient connectivity (arbitrary size, DDR) ----
    const int*     net_ptr,    // [num_nets+1] CSR
    const NodePin* pins,       // [num_pins] NET-major
    const NodePin* npins,      // [num_npins] NODE-major, movable+gradient-bearing only
    const float*   exp_lut,    // [lut_size] exp(-t) table
    NetBBox*       bb,         // [num_nets] scratch (kernel writes then reads)
    NetSums*       sums,       // [num_nets] scratch (kernel writes then reads)
    // ---- geometry / Nesterov state (movable first, [0,num_movable)) ----
    const NodeBox* node_box,   // [num_nodes] v_k anchor {x,y} + cell size {w,h}
    const coord_t* u_in,       // [num_movable] u_k committed position
    const float*   precond,    // [num_movable] preconditioner weight (1.0 = off, v1 default)
    // ---- outputs ----
    coord_t*       u_out,      // [num_movable] u_{k+1}
    coord_t*       v_out,      // [num_movable] v_{k+1} (through memory_writer)
    // ---- HPWL scalars ----
    float inv_gamma, float inv_lut_step, int lut_size, int num_nets, int num_npins,
    // ---- density scalars ----
    float bin_w, float bin_h, float target_density,
    // ---- iteration_update scalars ----
    float lambda, float alpha, float coeff, float die_xmax, float die_ymax,
    // ---- shared ----
    int num_movable, int num_nodes) {
#pragma HLS INTERFACE m_axi port=net_ptr  offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=pins     offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=npins    offset=slave bundle=gmem2
#pragma HLS INTERFACE m_axi port=exp_lut  offset=slave bundle=gmem3
#pragma HLS INTERFACE m_axi port=bb       offset=slave bundle=gmem4
#pragma HLS INTERFACE m_axi port=sums     offset=slave bundle=gmem5
#pragma HLS INTERFACE m_axi port=node_box offset=slave bundle=gmem6
#pragma HLS INTERFACE m_axi port=u_in     offset=slave bundle=gmem7
#pragma HLS INTERFACE m_axi port=precond  offset=slave bundle=gmem8
#pragma HLS INTERFACE m_axi port=u_out    offset=slave bundle=gmem9
#pragma HLS INTERFACE m_axi port=v_out    offset=slave bundle=gmem10
#pragma HLS INTERFACE s_axilite port=net_ptr        bundle=control
#pragma HLS INTERFACE s_axilite port=pins           bundle=control
#pragma HLS INTERFACE s_axilite port=npins          bundle=control
#pragma HLS INTERFACE s_axilite port=exp_lut        bundle=control
#pragma HLS INTERFACE s_axilite port=bb             bundle=control
#pragma HLS INTERFACE s_axilite port=sums           bundle=control
#pragma HLS INTERFACE s_axilite port=node_box       bundle=control
#pragma HLS INTERFACE s_axilite port=u_in           bundle=control
#pragma HLS INTERFACE s_axilite port=precond        bundle=control
#pragma HLS INTERFACE s_axilite port=u_out          bundle=control
#pragma HLS INTERFACE s_axilite port=v_out          bundle=control
#pragma HLS INTERFACE s_axilite port=inv_gamma      bundle=control
#pragma HLS INTERFACE s_axilite port=inv_lut_step   bundle=control
#pragma HLS INTERFACE s_axilite port=lut_size       bundle=control
#pragma HLS INTERFACE s_axilite port=num_nets       bundle=control
#pragma HLS INTERFACE s_axilite port=num_npins      bundle=control
#pragma HLS INTERFACE s_axilite port=bin_w          bundle=control
#pragma HLS INTERFACE s_axilite port=bin_h          bundle=control
#pragma HLS INTERFACE s_axilite port=target_density bundle=control
#pragma HLS INTERFACE s_axilite port=lambda         bundle=control
#pragma HLS INTERFACE s_axilite port=alpha          bundle=control
#pragma HLS INTERFACE s_axilite port=coeff          bundle=control
#pragma HLS INTERFACE s_axilite port=die_xmax       bundle=control
#pragma HLS INTERFACE s_axilite port=die_ymax       bundle=control
#pragma HLS INTERFACE s_axilite port=num_movable    bundle=control
#pragma HLS INTERFACE s_axilite port=num_nodes      bundle=control
#pragma HLS INTERFACE s_axilite port=return         bundle=control

    // node_pos for hpwl_CU is just node_box's {x,y} (the v_k probe anchor) -- extracted
    // on-chip rather than a separate DDR port, since this single kernel already has node_box.
    static coord_t node_pos_[ITER_MAX_NODES];
extract_pos:
    for (int i = 0; i < num_nodes; i++) {
#pragma HLS PIPELINE II=1
        node_pos_[i].x = node_box[i].x;
        node_pos_[i].y = node_box[i].y;
    }

    static coord_t g_hpwl_[ITER_MAX_NODES];
    hpwl_CU(node_pos_, net_ptr, pins, npins, exp_lut, bb, sums, g_hpwl_,
            inv_gamma, inv_lut_step, lut_size, num_nets, num_movable, num_npins);

    static float rho_[GRID * GRID], Ex_[GRID * GRID], Ey_[GRID * GRID];
    static float tA_[GRID * GRID], tB_[GRID * GRID];
    density_bin(node_box, rho_, num_movable, num_nodes, bin_w, bin_h, target_density);
    field_solve_pl(rho_, Ex_, Ey_, tA_, tB_);

    static coord_t g_density_[ITER_MAX_NODES];
    force_gather(node_box, Ex_, Ey_, g_density_, num_movable, bin_w, bin_h);

    iteration_step_df(g_hpwl_, g_density_, node_box, u_in, precond, u_out, v_out,
                      lambda, alpha, coeff, die_xmax, die_ymax, num_movable);
}
} // extern "C"
