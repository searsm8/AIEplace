// top.cpp -- pl_algo PL kernel.
//
// One kernel, modules selected by `mode` (host_interface.hpp top_mode). Bring-up
// scaffold: each module is verified independently through one kernel while we
// build toward the unified per-iteration datapath (Stage 5).
//   MODE_HPWL_GRAD   -> hpwl_CU    : HPWL gradient (vs sw_only computeHpwlPartials_CPU).
//   MODE_DENSITY_BIN -> density_bin : bin density rho (vs sw_only computeOverlaps).
//   MODE_DCT_1D      -> dct_1d     : 1D DCT via the AIE FFT (PL shuffle/twiddle, AIE
//                                    does only the forward FFT). First AIE-using mode.
// The inactive modules' buffers are still kernel args (the host binds 1-element
// dummies); each module touches only its own buffers/streams, so they are inert.
// The fft_to_aie/fft_from_aie AXIS ports connect to the AIE DensityFFTGraph PLIO
// via link.cfg (generate_link_cfg.py); they are HW-wired, not host-set.
//
// Natural typed m_axi pointers; 128-bit beat packing (formats.hpp) is a later
// throughput optimization (except the AIE streams, which are 128-bit AXIS).

#include "host_interface.hpp"
#include "formats.hpp"
#include "modules/hpwl_gradient.hpp"
#include "modules/density_bin.hpp"
#include "modules/dct_1d.hpp"
#include "modules/transpose.hpp"
#include "modules/dct_transpose.hpp"
#include "modules/spectral.hpp"
#include "modules/force_gather.hpp"
#include "modules/iteration_update.hpp"
#include "modules/memory_writer.hpp"
#include "modules/metrics.hpp"
#ifdef PL_FIELD_SOLVE
#include "modules/field_solve_pl.hpp"   // PL-only field solve (small-grid build only)
#endif

using namespace plalgo;

// Stage 5c dataflow region: iteration_update (producer) -> stream -> memory_writer (consumer).
// Kept as a dedicated function so #pragma HLS DATAFLOW sits at a canonical function-body top
// level (not inside top()'s mode if/else chain). u_out and coords_out are distinct DDR bundles.
static void iteration_step_df(const coord_t* g_hpwl, const coord_t* g_density,
                              const NodeBox* node_box, const coord_t* u_in,
                              const float* precond, coord_t* u_out, coord_t* coords_out,
                              float lambda, float alpha, float coeff,
                              float die_xmax, float die_ymax, int num_movable) {
#pragma HLS DATAFLOW
    hls::stream<coord_t> v_edge;
#pragma HLS STREAM variable=v_edge depth=64
    iteration_update(g_hpwl, g_density, node_box, u_in, precond, u_out,
                     lambda, alpha, coeff, die_xmax, die_ymax, num_movable, v_edge);
    memory_writer(coords_out, v_edge, num_movable);
}

extern "C" {
void top(
    // ---- HPWL gradient buffers (group_id 0-7) ----
    const coord_t* node_pos,
    const int*     net_ptr,
    const NodePin* pins,
    const NodePin* npins,
    const float*   exp_lut,
    NetBBox*       bb,
    NetSums*       sums,
    coord_t*       node_grad,
    // ---- density buffers (group_id 8-9) ----
    const NodeBox* node_box,
    float*         bin_density,
    // ---- 1D DCT buffers (group_id 10-11) ----
    const float*   dct_in,
    float*         dct_out,
    // ---- HPWL scalars ----
    float          inv_gamma,
    float          inv_lut_step,
    int            lut_size,
    int            num_nets,
    int            num_movable,
    int            num_npins,
    // ---- density scalars ----
    int            num_nodes,
    float          bin_w,
    float          bin_h,
    float          target_density,
    // ---- 1D DCT scalars ----
    int            dct_stage,
    int            num_frames,
    // ---- mode selector ----
    int            mode
#ifndef PL_ONLY
    ,
    // ---- AIE FFT pool streams: 8 lanes as SEPARATE named ports (HW-wired via link.cfg,
    // not host args). HLS does not support an array of hls::stream at the AXIS interface,
    // so the lanes are individual scalar streams fft_to_aie_<i> / fft_from_aie_<i>.
    // PL_ONLY (AIE=none) builds omit these ports entirely -- an RTL/hw_emu link cannot leave
    // AXIS ports dangling and a self-loop stream_connect is rejected, so a PL-only design must
    // not declare them. The AIE DCT modes are compiled out to match (see below). ----
    hls::stream<axis_t>& fft_to_aie_0, hls::stream<axis_t>& fft_to_aie_1,
    hls::stream<axis_t>& fft_to_aie_2, hls::stream<axis_t>& fft_to_aie_3,
    hls::stream<axis_t>& fft_to_aie_4, hls::stream<axis_t>& fft_to_aie_5,
    hls::stream<axis_t>& fft_to_aie_6, hls::stream<axis_t>& fft_to_aie_7,
    hls::stream<axis_t>& fft_from_aie_0, hls::stream<axis_t>& fft_from_aie_1,
    hls::stream<axis_t>& fft_from_aie_2, hls::stream<axis_t>& fft_from_aie_3,
    hls::stream<axis_t>& fft_from_aie_4, hls::stream<axis_t>& fft_from_aie_5,
    hls::stream<axis_t>& fft_from_aie_6, hls::stream<axis_t>& fft_from_aie_7
#endif
    )
{
 
/* 
 * DDR AXI4 master interfaces (m_axi) buffers (data to be sent into PL)
 */
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
// gmem10/gmem11: transpose overlaps per-tile-row bursts (transpose.hpp Option (a)) --
// size the outstanding-request buffers so the m_axi adapter keeps a tile's worth of row
// bursts in flight, hiding the ~70-cyc DDR latency instead of paying it per tile-row.
#pragma HLS INTERFACE m_axi port=dct_in      offset=slave bundle=gmem10 num_read_outstanding=32 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=dct_out     offset=slave bundle=gmem11 num_write_outstanding=32 max_write_burst_length=64


/* 
 * AXI4-Lite control interface (s_axilite) for kernel args and scalars
 */
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
#pragma HLS INTERFACE s_axilite port=dct_in         bundle=control
#pragma HLS INTERFACE s_axilite port=dct_out        bundle=control
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
#pragma HLS INTERFACE s_axilite port=dct_stage      bundle=control
#pragma HLS INTERFACE s_axilite port=num_frames     bundle=control
#pragma HLS INTERFACE s_axilite port=mode           bundle=control

/*
 * AXIS interfaces PL to AIE (for the 8-lane AIE FFT pool)
 */
#ifndef PL_ONLY
#pragma HLS INTERFACE axis port=fft_to_aie_0
#pragma HLS INTERFACE axis port=fft_to_aie_1
#pragma HLS INTERFACE axis port=fft_to_aie_2
#pragma HLS INTERFACE axis port=fft_to_aie_3
#pragma HLS INTERFACE axis port=fft_to_aie_4
#pragma HLS INTERFACE axis port=fft_to_aie_5
#pragma HLS INTERFACE axis port=fft_to_aie_6
#pragma HLS INTERFACE axis port=fft_to_aie_7
#pragma HLS INTERFACE axis port=fft_from_aie_0
#pragma HLS INTERFACE axis port=fft_from_aie_1
#pragma HLS INTERFACE axis port=fft_from_aie_2
#pragma HLS INTERFACE axis port=fft_from_aie_3
#pragma HLS INTERFACE axis port=fft_from_aie_4
#pragma HLS INTERFACE axis port=fft_from_aie_5
#pragma HLS INTERFACE axis port=fft_from_aie_6
#pragma HLS INTERFACE axis port=fft_from_aie_7
#endif // !PL_ONLY
#pragma HLS INTERFACE s_axilite port=return         bundle=control

    if (mode == MODE_DENSITY_BIN) {
        density_bin(node_box, bin_density, num_movable, num_nodes,
                    bin_w, bin_h, target_density);
    }
#ifndef PL_ONLY
    else if (mode == MODE_DCT_1D) {
        // single lane: use lane 0 of the pool (Stage 2 bring-up).
        dct_1d(dct_in, dct_out, num_frames, dct_stage, fft_to_aie_0, fft_from_aie_0);
    } else if (mode == MODE_DCT_ROWPASS) {
        // 8-lane row pass: DCT num_frames rows of the dct_in matrix (Stage 3a).
        dct_row_pass(dct_in, dct_out, num_frames,
                     fft_to_aie_0, fft_to_aie_1, fft_to_aie_2, fft_to_aie_3,
                     fft_to_aie_4, fft_to_aie_5, fft_to_aie_6, fft_to_aie_7,
                     fft_from_aie_0, fft_from_aie_1, fft_from_aie_2, fft_from_aie_3,
                     fft_from_aie_4, fft_from_aie_5, fft_from_aie_6, fft_from_aie_7);
    }
#endif // !PL_ONLY
    else if (mode == MODE_TRANSPOSE) {
        // GRID x GRID transpose (pure PL); dct_stage selects variant. Dimension is the
        // compile-time GRID (required for the DDR burst -- see transpose.hpp).
        if (dct_stage == 0) transpose_naive(dct_in, dct_out);
        else                transpose_band(dct_in, dct_out);
    }
#ifndef PL_ONLY
    else if (mode == MODE_DCT_TRANSPOSE) {
        // Stage 3c/4: fused transform row-pass + transpose. Transform all GRID rows via the
        // 8-lane pool, written transposed. dct_stage = transform_mode (TF_DCT/IDCT/IDXST);
        // dct_in -> dct_out (transposed). num_frames unused (N=GRID).
        dct_transpose_pass(dct_in, dct_out, dct_stage,
                           fft_to_aie_0, fft_to_aie_1, fft_to_aie_2, fft_to_aie_3,
                           fft_to_aie_4, fft_to_aie_5, fft_to_aie_6, fft_to_aie_7,
                           fft_from_aie_0, fft_from_aie_1, fft_from_aie_2, fft_from_aie_3,
                           fft_from_aie_4, fft_from_aie_5, fft_from_aie_6, fft_from_aie_7);
    }
#endif // !PL_ONLY
    else if (mode == MODE_SPECTRAL) {
        // Stage 4: spectral multiply a_uv -> one field. dct_stage = axis (0=Ex/w_u, 1=Ey/w_v).
        spectral_multiply(dct_in, dct_out, dct_stage);
    } else if (mode == MODE_FORCE_GATHER) {
        // Stage 5: per-node density gradient. eField_x = bin_density (gmem9), eField_y =
        // dct_in (gmem10), node geometry = node_box (gmem8) -> node_grad (gmem7).
        force_gather(node_box, bin_density, dct_in, node_grad, num_movable, bin_w, bin_h);
    } else if (mode == MODE_ITERATION_UPDATE) {
        // Stage 5c: one Nesterov step, streamed to the Memory Writer (single coords writer).
        // Port aliasing (see host_interface.hpp MODE_ITERATION_UPDATE): u_k=node_pos(0),
        // precond=exp_lut(4), g_hpwl=node_grad(7), {v_k,size}=node_box(8), g_density=dct_in(10);
        // u_{k+1}=dct_out(11), v_{k+1}=bin_density(9). Scalars: lambda=inv_gamma, alpha=
        // inv_lut_step, coeff=bin_w, die_xmax=bin_h, die_ymax=target_density.
        iteration_step_df(node_grad, (const coord_t*)dct_in, node_box, node_pos, exp_lut,
                          (coord_t*)dct_out, (coord_t*)bin_density,
                          inv_gamma, inv_lut_step, bin_w, bin_h, target_density, num_movable);
    } else if (mode == MODE_METRICS) {
        // Stage 5c: reduce {HPWL, overflow_sum}. HPWL from node_pos(0)/net_ptr(1)/pins(2);
        // overflow_sum from bin_density(9). Out: dct_out[0]=HPWL, dct_out[1]=overflow_sum.
        metrics(node_pos, net_ptr, pins, bin_density, num_nets, target_density, dct_out);
    }
#ifdef PL_FIELD_SOLVE
    else if (mode == MODE_FIELD_SOLVE_PL) {
        // PL-only density solve: rho (dct_in, gmem10) -> Ex (dct_out, gmem11), Ey (bin_density,
        // gmem9), the whole forward-DCT / spectral / inverse pipeline via fft_pl (no AIE). On-chip
        // scratch; only valid in the small-grid build (GRID*GRID must fit on-chip).
        static float rho_[GRID * GRID], Ex_[GRID * GRID], Ey_[GRID * GRID];
        static float tA_[GRID * GRID], tB_[GRID * GRID];
        for (int i = 0; i < GRID * GRID; i++) {
#pragma HLS PIPELINE II=1
            rho_[i] = dct_in[i];
        }
        field_solve_pl(rho_, Ex_, Ey_, tA_, tB_);
        for (int i = 0; i < GRID * GRID; i++) {
#pragma HLS PIPELINE II=1
            dct_out[i] = Ex_[i]; bin_density[i] = Ey_[i];
        }
    }
#endif
    else { // MODE_HPWL_GRAD
        hpwl_CU(node_pos, net_ptr, pins, npins, exp_lut, bb, sums, node_grad,
                inv_gamma, inv_lut_step, lut_size, num_nets, num_movable, num_npins);
    }
}
} // extern "C"
