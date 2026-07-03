// top.cpp -- pl_algo PL kernel.
//
// One kernel, modules selected by `mode` (host_interface.hpp top_mode). Bring-up
// scaffold: each module is verified independently through one kernel while we
// build toward the unified per-iteration datapath (Stage 5).
//   MODE_HPWL_GRAD   -> hpwl_CU    : HPWL gradient (vs markv1 computeHpwlPartials_CPU).
//   MODE_DENSITY_BIN -> density_bin : bin density rho (vs markv1 computeOverlaps).
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

using namespace plalgo;

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
    int            mode,
    // ---- AIE FFT pool streams: 8 lanes as SEPARATE named ports (HW-wired via link.cfg,
    // not host args). HLS does not support an array of hls::stream at the AXIS interface,
    // so the lanes are individual scalar streams fft_to_aie_<i> / fft_from_aie_<i>. ----
    hls::stream<axis_t>& fft_to_aie_0, hls::stream<axis_t>& fft_to_aie_1,
    hls::stream<axis_t>& fft_to_aie_2, hls::stream<axis_t>& fft_to_aie_3,
    hls::stream<axis_t>& fft_to_aie_4, hls::stream<axis_t>& fft_to_aie_5,
    hls::stream<axis_t>& fft_to_aie_6, hls::stream<axis_t>& fft_to_aie_7,
    hls::stream<axis_t>& fft_from_aie_0, hls::stream<axis_t>& fft_from_aie_1,
    hls::stream<axis_t>& fft_from_aie_2, hls::stream<axis_t>& fft_from_aie_3,
    hls::stream<axis_t>& fft_from_aie_4, hls::stream<axis_t>& fft_from_aie_5,
    hls::stream<axis_t>& fft_from_aie_6, hls::stream<axis_t>& fft_from_aie_7)
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
#pragma HLS INTERFACE s_axilite port=return         bundle=control

    if (mode == MODE_DENSITY_BIN) {
        density_bin(node_box, bin_density, num_movable, num_nodes,
                    bin_w, bin_h, target_density);
    } else if (mode == MODE_DCT_1D) {
        // single lane: use lane 0 of the pool (Stage 2 bring-up).
        dct_1d(dct_in, dct_out, num_frames, dct_stage, fft_to_aie_0, fft_from_aie_0);
    } else if (mode == MODE_DCT_ROWPASS) {
        // 8-lane row pass: DCT num_frames rows of the dct_in matrix (Stage 3a).
        dct_row_pass(dct_in, dct_out, num_frames,
                     fft_to_aie_0, fft_to_aie_1, fft_to_aie_2, fft_to_aie_3,
                     fft_to_aie_4, fft_to_aie_5, fft_to_aie_6, fft_to_aie_7,
                     fft_from_aie_0, fft_from_aie_1, fft_from_aie_2, fft_from_aie_3,
                     fft_from_aie_4, fft_from_aie_5, fft_from_aie_6, fft_from_aie_7);
    } else if (mode == MODE_TRANSPOSE) {
        // GRID x GRID transpose (pure PL); dct_stage selects variant. Dimension is the
        // compile-time GRID (required for the DDR burst -- see transpose.hpp).
        if (dct_stage == 0) transpose_naive(dct_in, dct_out);
        else                transpose_band(dct_in, dct_out);
    } else if (mode == MODE_DCT_TRANSPOSE) {
        // Stage 3c/4: fused transform row-pass + transpose. Transform all GRID rows via the
        // 8-lane pool, written transposed. dct_stage = transform_mode (TF_DCT/IDCT/IDXST);
        // dct_in -> dct_out (transposed). num_frames unused (N=GRID).
        dct_transpose_pass(dct_in, dct_out, dct_stage,
                           fft_to_aie_0, fft_to_aie_1, fft_to_aie_2, fft_to_aie_3,
                           fft_to_aie_4, fft_to_aie_5, fft_to_aie_6, fft_to_aie_7,
                           fft_from_aie_0, fft_from_aie_1, fft_from_aie_2, fft_from_aie_3,
                           fft_from_aie_4, fft_from_aie_5, fft_from_aie_6, fft_from_aie_7);
    } else if (mode == MODE_SPECTRAL) {
        // Stage 4: spectral multiply a_uv -> one field. dct_stage = axis (0=Ex/w_u, 1=Ey/w_v).
        spectral_multiply(dct_in, dct_out, dct_stage);
    } else if (mode == MODE_FORCE_GATHER) {
        // Stage 5: per-node density gradient. eField_x = bin_density (gmem9), eField_y =
        // dct_in (gmem10), node geometry = node_box (gmem8) -> node_grad (gmem7).
        force_gather(node_box, bin_density, dct_in, node_grad, num_movable, bin_w, bin_h);
    } else { // MODE_HPWL_GRAD
        hpwl_CU(node_pos, net_ptr, pins, npins, exp_lut, bb, sums, node_grad,
                inv_gamma, inv_lut_step, lut_size, num_nets, num_movable, num_npins);
    }
}
} // extern "C"
