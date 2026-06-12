// top.cpp -- top-level PL kernel for the pl_algo (PL-centric) AIEplace design.
//
// This kernel owns the per-iteration control flow. Every diagram block is a module with
// a documented I/O contract (see modules/*.hpp); the modules are black boxes for now and
// this file does nothing but declare the interfaces and sequence the modules.
//
// AIE does only the FFT (FFT_LANES-wide pool) and the HPWL gradient graph. Everything
// else lives here in the PL. All large matrices (bin density, Ex, Ey, 4 MB each at the
// 1024x1024 hardware grid) are DDR-resident scratch, passed as m_axi pointers.
//
// v1 runs one host call per iteration: the host owns the gamma/lambda schedule and the
// convergence test and passes them in each call. The internal iteration loop is kept so
// the loop can later absorb iterations when the policy migrates onto the PL.

#include "formats.hpp"
#include "modules/memory_writer.hpp"
#include "modules/hpwl_manager.hpp"
#include "modules/density_manager.hpp"
#include "modules/iteration_update.hpp"
#include "modules/metrics.hpp"

using namespace plalgo;

extern "C" {
void top(
    // ---- DDR buffers (m_axi) ----
    beat_t* coords,       // canonical node coordinates (read by managers, written by Memory Writer)
    beat_t* node_grad,    // per-node HPWL gradient (HPWL Manager -> Iteration Update)
    beat_t* node_state,   // Nesterov state (owned by Iteration Update)
    beat_t* net_data,     // net/pin connectivity (read-only)
    beat_t* bin_density,  // scratch density matrix (4 MB)
    beat_t* efield_x,     // Ex field matrix (4 MB)
    beat_t* efield_y,     // Ey field matrix (4 MB)
    beat_t* status,       // HPWL/overflow readback for host policy
    // ---- AIE FFT pool streams ----
    hls::stream<axis_t> fft_to_aie[FFT_LANES],
    hls::stream<axis_t> fft_from_aie[FFT_LANES],
    // ---- AIE HPWL graph streams ----
    hls::stream<axis_t>& hpwl_to_aie,
    hls::stream<axis_t>& hpwl_from_aie,
    // ---- scalar config / policy (s_axilite) ----
    int   num_nodes,
    int   num_nets,
    int   max_iters,
    float die_xmin, float die_xmax,
    float die_ymin, float die_ymax,
    float gamma,      // wirelength smoothing (host policy)
    float lambda,     // density weight   (host policy)
    float alpha)      // BB step length   (host policy / fed back)
{
// m_axi interfaces for DDR buffers. 
// The "bundle" annotations group them for AXI interconnect optimization
// the exact bundling can be revised once the design stabilizes.
#pragma HLS INTERFACE m_axi  port=coords      offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi  port=node_grad   offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi  port=node_state  offset=slave bundle=gmem2
#pragma HLS INTERFACE m_axi  port=net_data    offset=slave bundle=gmem3
#pragma HLS INTERFACE m_axi  port=bin_density offset=slave bundle=gmem4
#pragma HLS INTERFACE m_axi  port=efield_x    offset=slave bundle=gmem5
#pragma HLS INTERFACE m_axi  port=efield_y    offset=slave bundle=gmem6
#pragma HLS INTERFACE m_axi  port=status      offset=slave bundle=gmem7

// m_axis interfaces for AIE streams.
#pragma HLS INTERFACE m_axis port=fft_to_aie
#pragma HLS INTERFACE m_axis port=fft_from_aie
#pragma HLS INTERFACE m_axis port=hpwl_to_aie
#pragma HLS INTERFACE m_axis port=hpwl_from_aie

// s_axilite interfaces for scalar config and policy parameters. These are passed by the host on each call;
//  the internal iteration loop is retained so the PL can later absorb more iterations when the policy migrates onto the PL.
#pragma HLS INTERFACE s_axilite port=num_nodes bundle=control
#pragma HLS INTERFACE s_axilite port=num_nets  bundle=control
#pragma HLS INTERFACE s_axilite port=max_iters bundle=control
#pragma HLS INTERFACE s_axilite port=die_xmin  bundle=control
#pragma HLS INTERFACE s_axilite port=die_xmax  bundle=control
#pragma HLS INTERFACE s_axilite port=die_ymin  bundle=control
#pragma HLS INTERFACE s_axilite port=die_ymax  bundle=control
#pragma HLS INTERFACE s_axilite port=gamma     bundle=control
#pragma HLS INTERFACE s_axilite port=lambda    bundle=control
#pragma HLS INTERFACE s_axilite port=alpha     bundle=control
#pragma HLS INTERFACE s_axilite port=return    bundle=control

iter_loop:
    for (int it = 0; it < max_iters; it++) {
        // 1. HPWL wirelength gradient (AIE HPWL graph).
        hpwl_manager(coords, net_data, node_grad, gamma,
                     num_nodes, num_nets, hpwl_to_aie, hpwl_from_aie);

        // 2. Density -> E-field (PL pre/post around the AIE FFT pool; FSM inside).
        density_manager(coords, bin_density, efield_x, efield_y,
                        num_nodes, fft_to_aie, fft_from_aie);

        // 3. Combine + step + Nesterov -> stream updated coords to the Memory Writer.
        //    TODO: when implemented, make this pair an HLS DATAFLOW region so the
        //    producer/consumer stream is bounded; sequential here is a synthesis skeleton.
        hls::stream<axis_t> coords_out;
        iteration_update(node_grad, efield_x, efield_y, coords, node_state,
                         lambda, alpha, die_xmin, die_xmax, die_ymin, die_ymax,
                         num_nodes, coords_out);
        memory_writer(coords, coords_out, num_nodes);

        // 4. Reduce HPWL / overflow for the host policy.
        float hpwl_val, overflow_val;
        metrics(coords, net_data, bin_density, num_nodes, num_nets,
                &hpwl_val, &overflow_val);

        union { float f; unsigned int u; } cvt_h, cvt_o;
        cvt_h.f = hpwl_val;
        cvt_o.f = overflow_val;
        beat_t s = 0;
        s.range(31, 0)  = cvt_h.u;
        s.range(63, 32) = cvt_o.u;
        status[it] = s;
    }
}
} // extern "C"
