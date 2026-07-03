#ifndef PL_ALGO_DRIVER_HPP
#define PL_ALGO_DRIVER_HPP

// Driver.hpp -- XRT driver for the v0 HPWL kernel. Uploads the packed design to
// the device, runs the `top` kernel, and reads back the total HPWL. Only built
// when the host is compiled with BUILD_XRT (USE_XILINX_XRT).

#include "PackedDesign.hpp"

namespace plalgo {

// Run the v0 HPWL kernel on the device described by xclbin_path (e.g. an sw_emu
// or hw build). Uploads node_pos/net_ptr/pins, executes top(), returns the
// single float HPWL the kernel writes back.
//
// xclbin_path is a const char* (not std::string) on purpose: this header is
// included by parser-side code built with the old GLIBCXX ABI, while Driver.cpp
// is built with the new ABI to match libxrt. Keeping std::string off this
// boundary is what lets the two ABIs coexist in one binary (see PackedDesign.hpp).
float runHpwlKernel(const PackedDesign& pk, const char* xclbin_path);

// Run the PL HPWL gradient compute unit (hpwl_CU) on the device. Uploads the
// packed design + the exp LUT, executes top(), and writes the per-movable-node
// gradient (dW/dx, dW/dy) into node_grad (caller-allocated, num_movable entries).
// PL-only path (AIE=none) -- no graph.
void runHpwlGradCU(const PackedDesign& pk,
                   const float* exp_lut, int lut_size,
                   float inv_gamma, float inv_lut_step,
                   coord_t* node_grad,
                   const char* xclbin_path);

// Run the PL density binning module (density_bin) on the device. Uploads the
// node_box geometry, executes top() in MODE_DENSITY_BIN, and writes the
// GRID x GRID bin-density grid rho (x-major, rho[x*GRID+y]) into bin_density
// (caller-allocated, DENSITY_NBINS floats). PL-only path (AIE=none).
void runDensityBin(const PackedDesign& pk,
                   float bin_w, float bin_h, float target_density,
                   float* bin_density,
                   const char* xclbin_path);

// Run the 1D DCT bring-up (MODE_DCT_1D) for num_frames real rows of FFT_PTS points.
// One device/graph session runs BOTH stages: stage 0 (FFT passthrough) -> out_fft
// (num_frames*FFT_PTS*2 floats, complex {re,im}); stage 1 (full DCT) -> out_dct
// (num_frames*FFT_PTS floats, real). Both caller-allocated. AIE-using (AIE=pl_algo).
void runDCT1D(const float* dct_in, int num_frames,
              float* out_fft, float* out_dct, const char* xclbin_path);

// Run the 8-lane row-DCT pass (MODE_DCT_ROWPASS, Stage 3a): DCT every row of mat_in
// (num_rows x FFT_PTS, row-major real) through the AIE FFT pool, into mat_out
// (caller-allocated, same size). num_rows must be a multiple of DENSITY_LANES.
void runDCTRowPass(const float* mat_in, int num_rows,
                   float* mat_out, const char* xclbin_path);

// Run the PL matrix transpose (MODE_TRANSPOSE, Stage 3b) for the N x N row-major
// float matrix `in`, BOTH variants in one device session: out_naive (variant 0) and
// out_tiled (variant 1), each caller-allocated (N*N floats). Pure PL (no AIE). N must
// be a multiple of the tile size (32) for the tiled variant.
void runTranspose(const float* in, int N,
                  float* out_naive, float* out_tiled, const char* xclbin_path);

// Run the fused DCT+transpose pass (MODE_DCT_TRANSPOSE, Stage 3c): DCT every row of the
// N x N row-major real matrix mat_in through the AIE FFT pool, writing the result
// TRANSPOSED into mat_out (caller-allocated, N*N floats). N = GRID (square). AIE-using.
void runDctTranspose(const float* mat_in, int N,
                     float* mat_out, const char* xclbin_path);

// Forward 2D DCT (Stage 3c composition): a_uv = C*rho*C^T via TWO fused DCT+transpose
// passes (scratch crosses via host). rho, a_uv caller-allocated (N*N floats). AIE-using.
void runDct2D(const float* rho, int N, float* a_uv, const char* xclbin_path);

// Stage 4: one fused transform+transpose pass with an explicit transform_mode
// (tf = TFH_DCT/TFH_IDCT/TFH_IDXST). mat_in, mat_out caller-allocated (N*N floats). AIE-using.
void runXformTranspose(const float* mat_in, int N, int tf,
                       float* mat_out, const char* xclbin_path);

// Stage 4: spectral multiply a_uv -> BOTH fields (Ex_hat = w_u, Ey_hat = w_v) in one
// session. a_uv, Ex_hat, Ey_hat caller-allocated (N*N floats). Pure PL (no AIE graph).
void runSpectral(const float* a_uv, int N,
                 float* Ex_hat, float* Ey_hat, const char* xclbin_path);

// Stage 4: full electrostatic field solve (rho -> Ex, Ey) in one device/graph session:
// forward 2D DCT, spectral multiply, and the four inverse IDCT/IDXST passes. rho, Ex, Ey
// caller-allocated (N*N floats). AIE-using. Mirrors markv1 compute_eField_DCT.
void runField(const float* rho, int N, float* Ex, float* Ey, const char* xclbin_path);

// Stage 5: force gather -- per-movable-node density gradient = sum_bins overlap_area*eField.
// node_box[num_nodes], eField_x/eField_y[GRID*GRID] (x-major), node_grad[num_movable] out
// (caller-allocated). Pure PL (no AIE graph).
void runForceGather(const NodeBox* node_box, int num_nodes, int num_movable,
                    const float* eField_x, const float* eField_y,
                    float bin_w, float bin_h,
                    coord_t* node_grad, const char* xclbin_path);

// Stage 5b: the full density gradient in one device/graph session -- node geometry ->
// density_bin -> forward 2D DCT -> spectral -> inverse -> force_gather -> per-node density
// gradient (node_grad[num_movable], caller-allocated). AIE-using.
void runDensityGradient(const NodeBox* node_box, int num_nodes, int num_movable,
                        float bin_w, float bin_h, float target_density,
                        coord_t* node_grad, const char* xclbin_path);

} // namespace plalgo

#endif // PL_ALGO_DRIVER_HPP
