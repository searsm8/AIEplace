// Driver.cpp -- native XRT host driver for the v0 HPWL kernel. See Driver.hpp.
//
// The kernel signature (pl/src/pl_algo/src/top.cpp) is:
//     void top(const coord_t* node_pos,   // arg 0
//              const int*     net_ptr,    // arg 1
//              const NodePin* pins,     // arg 2
//              float*         result,     // arg 3
//              int            num_nets)   // arg 4 (scalar)
// Buffer args 0..3 each live in a device memory bank selected by group_id(i).

#include "Driver.hpp"
#include "host_interface.hpp"
#include "Placement.hpp"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"
#include "experimental/xrt_graph.h"   // xrt::graph (AIE control, MODE_DCT_1D)

namespace plalgo {

float runHpwlKernel(const PackedDesign& pk, const char* xclbin_path) {
    const int num_nets = pk.header.num_nets;

    // Buffer sizes follow the contract: arrays of the exact POD types the host
    // packed, so a flat memcpy into each device buffer is a faithful upload.
    const size_t node_bytes = pk.node_pos.size() * sizeof(coord_t);
    const size_t nptr_bytes = pk.net_ptr.size()  * sizeof(int32_t);
    const size_t pins_bytes = pk.pins.size()     * sizeof(NodePin);
    const size_t res_bytes  = sizeof(float);

    // ---- open device, load xclbin, get the kernel ----
    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");

    // ---- allocate device buffers in each arg's memory bank ----
    xrt::bo bo_node = xrt::bo(device, node_bytes, top.group_id(0));
    xrt::bo bo_nptr = xrt::bo(device, nptr_bytes, top.group_id(1));
    xrt::bo bo_pins = xrt::bo(device, pins_bytes, top.group_id(2));
    xrt::bo bo_res  = xrt::bo(device, res_bytes,  top.group_id(3));

    // ---- fill input buffers from the packed design, push to device ----
    std::memcpy(bo_node.map<void*>(), pk.node_pos.data(), node_bytes);
    std::memcpy(bo_nptr.map<void*>(), pk.net_ptr.data(),  nptr_bytes);
    std::memcpy(bo_pins.map<void*>(), pk.pins.data(),     pins_bytes);

    bo_node.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_nptr.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_pins.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // ---- run top(node_pos, net_ptr, pins, result, num_nets) ----
    xrt::run run = top(bo_node, bo_nptr, bo_pins, bo_res, num_nets);
    run.wait(); // blocks until ap_done signal

    // ---- pull the result back ----
    bo_res.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    return *bo_res.map<float*>();
}

void runHpwlGradCU(const PackedDesign& pk,
                   const float* exp_lut, int lut_size,
                   float inv_gamma, float inv_lut_step,
                   coord_t* node_grad,
                   const char* xclbin_path) {
    const int    M          = pk.header.num_movable;
    const int    num_nets   = pk.header.num_nets;
    const int    num_npins  = (int)pk.npins.size();
    const size_t node_bytes  = pk.node_pos.size() * sizeof(coord_t);
    const size_t nptr_bytes  = pk.net_ptr.size()  * sizeof(int32_t);
    const size_t pins_bytes  = pk.pins.size()     * sizeof(NodePin);
    const size_t npins_bytes = pk.npins.size()    * sizeof(NodePin);
    const size_t lut_bytes   = (size_t)lut_size   * sizeof(float);
    const size_t bb_bytes    = (size_t)num_nets   * sizeof(NetBBox);
    const size_t sums_bytes  = (size_t)num_nets   * sizeof(NetSums);
    const size_t grad_bytes  = (size_t)M          * sizeof(coord_t);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");

    // top args: 0=node_pos 1=net_ptr 2=pins 3=npins 4=exp_lut 5=bb 6=sums
    //           7=node_grad 8=node_box 9=bin_density (then scalars, then mode).
    // bb/sums are device-only scratch; the density buffers (8,9) are inert in
    // MODE_HPWL_GRAD so they get 1-element dummies.
    xrt::bo bo_node  = xrt::bo(device, node_bytes,  top.group_id(0));
    xrt::bo bo_nptr  = xrt::bo(device, nptr_bytes,  top.group_id(1));
    xrt::bo bo_pins  = xrt::bo(device, pins_bytes,  top.group_id(2));
    xrt::bo bo_npins = xrt::bo(device, npins_bytes, top.group_id(3));
    xrt::bo bo_lut   = xrt::bo(device, lut_bytes,   top.group_id(4));
    xrt::bo bo_bb    = xrt::bo(device, bb_bytes,    top.group_id(5));
    xrt::bo bo_sums  = xrt::bo(device, sums_bytes,  top.group_id(6));
    xrt::bo bo_grad  = xrt::bo(device, grad_bytes,  top.group_id(7));
    xrt::bo bo_box   = xrt::bo(device, sizeof(NodeBox), top.group_id(8));  // inert dummy
    xrt::bo bo_bd    = xrt::bo(device, sizeof(float),   top.group_id(9));  // inert dummy
    xrt::bo bo_din   = xrt::bo(device, sizeof(float),   top.group_id(10)); // inert dummy
    xrt::bo bo_dout  = xrt::bo(device, sizeof(float),   top.group_id(11)); // inert dummy

    std::memcpy(bo_node.map<void*>(),  pk.node_pos.data(), node_bytes);
    std::memcpy(bo_nptr.map<void*>(),  pk.net_ptr.data(),  nptr_bytes);
    std::memcpy(bo_pins.map<void*>(),  pk.pins.data(),     pins_bytes);
    std::memcpy(bo_npins.map<void*>(), pk.npins.data(),    npins_bytes);
    std::memcpy(bo_lut.map<void*>(),   exp_lut,            lut_bytes);

    bo_node.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_nptr.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_pins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_npins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_lut.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    xrt::run run = top(bo_node, bo_nptr, bo_pins, bo_npins, bo_lut, bo_bb, bo_sums, bo_grad,
                       bo_box, bo_bd, bo_din, bo_dout,
                       inv_gamma, inv_lut_step, lut_size, num_nets, M, num_npins,
                       pk.header.num_nodes, 1.0f, 1.0f, 1.0f,   // density scalars (unused here)
                       0, 0,                                    // dct scalars (unused here)
                       (int)MODE_HPWL_GRAD);
    run.wait();

    bo_grad.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(node_grad, bo_grad.map<void*>(), grad_bytes);
}

void runDensityBin(const PackedDesign& pk,
                   float bin_w, float bin_h, float target_density,
                   float* bin_density,
                   const char* xclbin_path) {
    const int    N         = pk.header.num_nodes;
    const int    M         = pk.header.num_movable;
    const size_t box_bytes = (size_t)N * sizeof(NodeBox);
    const size_t bd_bytes  = (size_t)DENSITY_NBINS * sizeof(float);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");

    // Real density buffers (8,9); HPWL buffers (0-7) are inert in MODE_DENSITY_BIN
    // so they get 1-element dummies.
    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo bo_box = xrt::bo(device, box_bytes, top.group_id(8));
    xrt::bo bo_bd  = xrt::bo(device, bd_bytes,  top.group_id(9));
    xrt::bo bo_din  = xrt::bo(device, sizeof(float), top.group_id(10)); // inert dummy
    xrt::bo bo_dout = xrt::bo(device, sizeof(float), top.group_id(11)); // inert dummy

    std::memcpy(bo_box.map<void*>(), pk.node_box.data(), box_bytes);
    bo_box.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                       bo_box, bo_bd, bo_din, bo_dout,
                       0.0f, 0.0f, 0, 0, M, 0,                 // HPWL scalars (unused here)
                       N, bin_w, bin_h, target_density,
                       0, 0,                                   // dct scalars (unused here)
                       (int)MODE_DENSITY_BIN);
    run.wait();

    bo_bd.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(bin_density, bo_bd.map<void*>(), bd_bytes);
}

void runDCT1D(const float* dct_in, int num_frames,
              float* out_fft, float* out_dct, const char* xclbin_path) {
    const size_t in_floats  = (size_t)num_frames * FFT_PTS;
    const size_t in_bytes   = in_floats * sizeof(float);
    const size_t out_max    = in_floats * 2;             // stage 0 (complex) is the larger
    const size_t out_bytes  = out_max * sizeof(float);

    // One device/graph session runs BOTH stages back-to-back (avoids reopening the
    // sw_emu device/AIE-sim twice in one process).
    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::graph  fft(device, uuid, "density_fft_graph"); // AIE graph instance name (TopGraph.cpp)

    // Real DCT buffers (groups 10,11); HPWL (0-7) + density (8,9) inert dummies.
    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d_box  = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo d_bd   = xrt::bo(device, sizeof(float),   top.group_id(9));
    xrt::bo bo_in  = xrt::bo(device, in_bytes,  top.group_id(10));
    xrt::bo bo_out = xrt::bo(device, out_bytes, top.group_id(11));

    std::memcpy(bo_in.map<void*>(), dct_in, in_bytes);
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    auto run_stage = [&](int stage, float* dst, size_t dst_floats) {
        // Start the AIE FFT graph (num_frames windows), run top (PL streams the
        // frames to/from the AIE), wait on both. (toy_aie pattern.)
        fft.run(num_frames);
        xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                           d_box, d_bd, bo_in, bo_out,
                           0.0f, 0.0f, 0, 0, 0, 0,             // HPWL scalars (unused here)
                           0, 1.0f, 1.0f, 1.0f,                // density scalars (unused here)
                           stage, num_frames,
                           (int)MODE_DCT_1D);
        run.wait();
        fft.wait();
        bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(dst, bo_out.map<void*>(), dst_floats * sizeof(float));
    };

    run_stage(DCT_STAGE_FFT, out_fft, in_floats * 2);  // raw complex FFT
    run_stage(DCT_STAGE_DCT, out_dct, in_floats);      // real DCT
}

void runDCTRowPass(const float* mat_in, int num_rows,
                   float* mat_out, const char* xclbin_path) {
    const size_t mat_floats = (size_t)num_rows * FFT_PTS;
    const size_t mat_bytes  = mat_floats * sizeof(float);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::graph  fft(device, uuid, "density_fft_graph");

    // Real matrix buffers (groups 10,11); HPWL (0-7) + density (8,9) inert dummies.
    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d_box  = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo d_bd   = xrt::bo(device, sizeof(float),   top.group_id(9));
    xrt::bo bo_in  = xrt::bo(device, mat_bytes, top.group_id(10));
    xrt::bo bo_out = xrt::bo(device, mat_bytes, top.group_id(11));

    std::memcpy(bo_in.map<void*>(), mat_in, mat_bytes);
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // Each graph iteration = DENSITY_LANES instances x 1 frame = DENSITY_LANES rows.
    fft.run(num_rows / DENSITY_LANES);
    xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                       d_box, d_bd, bo_in, bo_out,
                       0.0f, 0.0f, 0, 0, 0, 0,                 // HPWL scalars (unused here)
                       0, 1.0f, 1.0f, 1.0f,                    // density scalars (unused here)
                       0, num_rows,                            // dct_stage unused; num_frames = num_rows
                       (int)MODE_DCT_ROWPASS);
    run.wait();
    fft.wait();

    bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(mat_out, bo_out.map<void*>(), mat_bytes);
}

void runTranspose(const float* in, int N,
                  float* out_naive, float* out_tiled, const char* xclbin_path) {
    const size_t bytes = (size_t)N * N * sizeof(float);

    // One device session runs BOTH variants (pure PL: no AIE graph used).
    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");

    // Real matrix buffers (groups 10,11); everything else inert dummies.
    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d_box  = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo d_bd   = xrt::bo(device, sizeof(float),   top.group_id(9));
    xrt::bo bo_in  = xrt::bo(device, bytes, top.group_id(10));
    xrt::bo bo_out = xrt::bo(device, bytes, top.group_id(11));

    std::memcpy(bo_in.map<void*>(), in, bytes);
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    auto run_variant = [&](int variant, float* dst) {
        xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                           d_box, d_bd, bo_in, bo_out,
                           0.0f, 0.0f, 0, 0, 0, 0,     // HPWL scalars (unused)
                           0, 1.0f, 1.0f, 1.0f,        // density scalars (unused)
                           variant, N,                 // dct_stage = variant (0 naive / 1 tiled), num_frames = N
                           (int)MODE_TRANSPOSE);
        run.wait();
        bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(dst, bo_out.map<void*>(), bytes);
    };

    run_variant(0, out_naive);
    run_variant(1, out_tiled);
}

void runDctTranspose(const float* mat_in, int N,
                     float* mat_out, const char* xclbin_path) {
    const size_t mat_bytes = (size_t)N * N * sizeof(float);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::graph  fft(device, uuid, "density_fft_graph");

    // Real matrix buffers (groups 10,11); HPWL (0-7) + density (8,9) inert dummies.
    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d_box  = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo d_bd   = xrt::bo(device, sizeof(float),   top.group_id(9));
    xrt::bo bo_in  = xrt::bo(device, mat_bytes, top.group_id(10));
    xrt::bo bo_out = xrt::bo(device, mat_bytes, top.group_id(11));

    std::memcpy(bo_in.map<void*>(), mat_in, mat_bytes);
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // Each graph iteration = DENSITY_LANES instances x 1 frame = DENSITY_LANES rows.
    fft.run(N / DENSITY_LANES);
    xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                       d_box, d_bd, bo_in, bo_out,
                       0.0f, 0.0f, 0, 0, 0, 0,                 // HPWL scalars (unused here)
                       0, 1.0f, 1.0f, 1.0f,                    // density scalars (unused here)
                       (int)TFH_DCT, N,                        // dct_stage = transform_mode; num_frames = N
                       (int)MODE_DCT_TRANSPOSE);
    run.wait();
    fft.wait();

    bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(mat_out, bo_out.map<void*>(), mat_bytes);
}

void runDct2D(const float* rho, int N, float* a_uv, const char* xclbin_path) {
    const size_t mat_bytes = (size_t)N * N * sizeof(float);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::graph  fft(device, uuid, "density_fft_graph");

    // Real matrix buffers (groups 10,11); HPWL (0-7) + density (8,9) inert dummies.
    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d_box  = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo d_bd   = xrt::bo(device, sizeof(float),   top.group_id(9));
    xrt::bo bo_in  = xrt::bo(device, mat_bytes, top.group_id(10));   // dct_in  (gmem10)
    xrt::bo bo_out = xrt::bo(device, mat_bytes, top.group_id(11));   // dct_out (gmem11)

    // One fused pass: src -> dst = transpose(rowDCT(src)). Scratch crosses via host so we
    // never need one buffer bound to both gmem10 (in) and gmem11 (out).
    auto run_pass = [&](const float* src, float* dst) {
        std::memcpy(bo_in.map<void*>(), src, mat_bytes);
        bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        fft.run(N / DENSITY_LANES);
        xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                           d_box, d_bd, bo_in, bo_out,
                           0.0f, 0.0f, 0, 0, 0, 0,                 // HPWL scalars (unused)
                           0, 1.0f, 1.0f, 1.0f,                    // density scalars (unused)
                           (int)TFH_DCT, N,                        // dct_stage = transform_mode; num_frames = N
                           (int)MODE_DCT_TRANSPOSE);
        run.wait();
        fft.wait();
        bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(dst, bo_out.map<void*>(), mat_bytes);
    };

    // Forward 2D DCT = two fused passes: rho -> C*rho^T -> C*rho*C^T = a_uv.
    std::vector<float> scratch((size_t)N * N);
    run_pass(rho, scratch.data());     // pass 1: rho     -> scratch
    run_pass(scratch.data(), a_uv);    // pass 2: scratch -> a_uv
}

// ---------------------------------------------------------------------------
// Stage 4: inverse field solve (spectral multiply + IDCT/IDXST passes).
// ---------------------------------------------------------------------------

// One fused transform+transpose pass with an explicit transform_mode (tf = TFH_DCT/IDCT/
// IDXST). Generalizes runDctTranspose; used by the isolated 4a/4b verifies.
void runXformTranspose(const float* mat_in, int N, int tf,
                       float* mat_out, const char* xclbin_path) {
    const size_t mat_bytes = (size_t)N * N * sizeof(float);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::graph  fft(device, uuid, "density_fft_graph");

    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d_box  = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo d_bd   = xrt::bo(device, sizeof(float),   top.group_id(9));
    xrt::bo bo_in  = xrt::bo(device, mat_bytes, top.group_id(10));
    xrt::bo bo_out = xrt::bo(device, mat_bytes, top.group_id(11));

    std::memcpy(bo_in.map<void*>(), mat_in, mat_bytes);
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    fft.run(N / DENSITY_LANES);
    xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                       d_box, d_bd, bo_in, bo_out,
                       0.0f, 0.0f, 0, 0, 0, 0,                 // HPWL scalars (unused)
                       0, 1.0f, 1.0f, 1.0f,                    // density scalars (unused)
                       tf, N,                                  // dct_stage = transform_mode; num_frames = N
                       (int)MODE_DCT_TRANSPOSE);
    run.wait();
    fft.wait();

    bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(mat_out, bo_out.map<void*>(), mat_bytes);
}

// Spectral multiply: a_uv -> BOTH fields (Ex_hat = axis 0/w_u, Ey_hat = axis 1/w_v), in one
// device session (a_uv stays resident in bo_in). Pure PL (no AIE graph). Used by the 4c verify.
void runSpectral(const float* a_uv, int N,
                 float* Ex_hat, float* Ey_hat, const char* xclbin_path) {
    const size_t mat_bytes = (size_t)N * N * sizeof(float);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");

    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d_box  = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo d_bd   = xrt::bo(device, sizeof(float),   top.group_id(9));
    xrt::bo bo_in  = xrt::bo(device, mat_bytes, top.group_id(10));
    xrt::bo bo_out = xrt::bo(device, mat_bytes, top.group_id(11));

    std::memcpy(bo_in.map<void*>(), a_uv, mat_bytes);
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    auto run_axis = [&](int axis, float* dst) {
        xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                           d_box, d_bd, bo_in, bo_out,
                           0.0f, 0.0f, 0, 0, 0, 0,             // HPWL scalars (unused)
                           0, 1.0f, 1.0f, 1.0f,                // density scalars (unused)
                           axis, N,                            // dct_stage = axis; num_frames = N
                           (int)MODE_SPECTRAL);
        run.wait();
        bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(dst, bo_out.map<void*>(), mat_bytes);
    };
    run_axis(0, Ex_hat);
    run_axis(1, Ey_hat);
}

// Full electrostatic field solve, all in ONE device/graph session (sw_emu can't reopen the
// device/AIE-sim in one process). rho -> a_uv (2 fwd DCT passes) -> spectral (Ex_hat, Ey_hat)
// -> inverse passes:  Ex = fused_IDXST(fused_IDCT(Ex_hat)),  Ey = fused_IDCT(fused_IDXST(Ey_hat)).
// Each fused pass = transform-rows-then-transpose, mirroring compute_eField_DCT exactly.
void runField(const float* rho, int N, float* Ex, float* Ey, const char* xclbin_path) {
    const size_t mat_bytes = (size_t)N * N * sizeof(float);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::graph  fft(device, uuid, "density_fft_graph");

    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d_box  = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo d_bd   = xrt::bo(device, sizeof(float),   top.group_id(9));
    xrt::bo bo_in  = xrt::bo(device, mat_bytes, top.group_id(10));
    xrt::bo bo_out = xrt::bo(device, mat_bytes, top.group_id(11));

    // One pass: src -> dst. mode selects DCT/IDCT/IDXST fused pass (uses AIE) or spectral
    // (PL-only); sub = transform_mode or axis. Scratch crosses via host (never bind one
    // buffer to both gmem10-in and gmem11-out).
    auto run_pass = [&](const float* src, float* dst, int mode, int sub) {
        std::memcpy(bo_in.map<void*>(), src, mat_bytes);
        bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        if (mode == (int)MODE_DCT_TRANSPOSE) fft.run(N / DENSITY_LANES);
        xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                           d_box, d_bd, bo_in, bo_out,
                           0.0f, 0.0f, 0, 0, 0, 0,             // HPWL scalars (unused)
                           0, 1.0f, 1.0f, 1.0f,                // density scalars (unused)
                           sub, N,                             // dct_stage = transform_mode/axis; num_frames = N
                           mode);
        run.wait();
        if (mode == (int)MODE_DCT_TRANSPOSE) fft.wait();
        bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(dst, bo_out.map<void*>(), mat_bytes);
    };

    std::vector<float> t1((size_t)N * N), a_uv((size_t)N * N);
    std::vector<float> Ex_hat((size_t)N * N), Ey_hat((size_t)N * N);
    std::vector<float> tE((size_t)N * N), tY((size_t)N * N);

    // forward 2D DCT: rho -> a_uv
    run_pass(rho,       t1.data(),     (int)MODE_DCT_TRANSPOSE, (int)TFH_DCT);
    run_pass(t1.data(), a_uv.data(),   (int)MODE_DCT_TRANSPOSE, (int)TFH_DCT);
    // spectral multiply: a_uv -> Ex_hat, Ey_hat
    run_pass(a_uv.data(), Ex_hat.data(), (int)MODE_SPECTRAL, 0);
    run_pass(a_uv.data(), Ey_hat.data(), (int)MODE_SPECTRAL, 1);
    // inverse: Ex = IDXST_x(IDCT_y(Ex_hat)), Ey = IDCT_x(IDXST_y(Ey_hat))
    run_pass(Ex_hat.data(), tE.data(), (int)MODE_DCT_TRANSPOSE, (int)TFH_IDCT);
    run_pass(tE.data(),     Ex,        (int)MODE_DCT_TRANSPOSE, (int)TFH_IDXST);
    run_pass(Ey_hat.data(), tY.data(), (int)MODE_DCT_TRANSPOSE, (int)TFH_IDXST);
    run_pass(tY.data(),     Ey,        (int)MODE_DCT_TRANSPOSE, (int)TFH_IDCT);
}

// Stage 5: force gather -- per-movable-node density gradient = sum_bins overlap_area*eField.
// Ports: node_grad out (gmem7), node_box (gmem8), eField_x = bin_density (gmem9), eField_y =
// dct_in (gmem10). Pure PL (no AIE graph).
void runForceGather(const NodeBox* node_box, int num_nodes, int num_movable,
                    const float* eField_x, const float* eField_y,
                    float bin_w, float bin_h,
                    coord_t* node_grad, const char* xclbin_path) {
    const size_t box_bytes  = (size_t)num_nodes    * sizeof(NodeBox);
    const size_t fld_bytes  = (size_t)DENSITY_NBINS * sizeof(float);
    const size_t grad_bytes = (size_t)num_movable  * sizeof(coord_t);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");

    // Real buffers: node_grad (7, out), node_box (8), eField_x (9), eField_y (10). Rest dummies.
    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo bo_grad = xrt::bo(device, grad_bytes, top.group_id(7));
    xrt::bo bo_box  = xrt::bo(device, box_bytes,  top.group_id(8));
    xrt::bo bo_efx  = xrt::bo(device, fld_bytes,  top.group_id(9));
    xrt::bo bo_efy  = xrt::bo(device, fld_bytes,  top.group_id(10));
    xrt::bo bo_dout = xrt::bo(device, sizeof(float), top.group_id(11)); // inert dummy

    std::memcpy(bo_box.map<void*>(), node_box, box_bytes);
    std::memcpy(bo_efx.map<void*>(), eField_x, fld_bytes);
    std::memcpy(bo_efy.map<void*>(), eField_y, fld_bytes);
    bo_box.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_efx.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_efy.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, bo_grad,
                       bo_box, bo_efx, bo_efy, bo_dout,
                       0.0f, 0.0f, 0, 0, num_movable, 0,      // HPWL scalars: num_movable used
                       num_nodes, bin_w, bin_h, 0.0f,         // density scalars: bin_w/bin_h used
                       0, 0,                                  // dct scalars (unused)
                       (int)MODE_FORCE_GATHER);
    run.wait();

    bo_grad.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(node_grad, bo_grad.map<void*>(), grad_bytes);
}

// PL-only field solve: rho -> Ex, Ey via fft_pl (no AIE). rho = dct_in (gmem10);
// Ex = dct_out (gmem11); Ey = bin_density (gmem9). NxN grids, N = DENSITY_GRID (small-grid build).
void runFieldSolvePl(const float* rho, float* Ex, float* Ey, const char* xclbin_path) {
    const size_t mat_bytes = (size_t)DENSITY_NBINS * sizeof(float);
    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::bo d0 = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d1 = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d2 = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d3 = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d4 = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d5 = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d6 = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d7 = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d8 = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo bo_ey  = xrt::bo(device, mat_bytes, top.group_id(9));   // Ey  out
    xrt::bo bo_rho = xrt::bo(device, mat_bytes, top.group_id(10));  // rho in
    xrt::bo bo_ex  = xrt::bo(device, mat_bytes, top.group_id(11));  // Ex  out
    std::memcpy(bo_rho.map<void*>(), rho, mat_bytes);
    bo_rho.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    xrt::run run = top(d0, d1, d2, d3, d4, d5, d6, d7, d8, bo_ey, bo_rho, bo_ex,
                       0.0f, 0.0f, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0,
                       (int)MODE_FIELD_SOLVE_PL);
    run.wait();
    bo_ex.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    bo_ey.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(Ex, bo_ex.map<void*>(), mat_bytes);
    std::memcpy(Ey, bo_ey.map<void*>(), mat_bytes);
}

// Stage 5b: the full density gradient in ONE device/graph session -- node geometry ->
// density_bin (rho) -> forward 2D DCT (a_uv) -> spectral (Ex_hat,Ey_hat) -> inverse
// (Ex,Ey) -> force_gather -> per-movable-node density gradient. Intermediate matrices
// cross via host. Ports: node_grad(7), node_box(8), bin_density(9) [rho then eField_x],
// dct_in(10) [field in/out then eField_y], dct_out(11) [field out]. AIE-using.
void runDensityGradient(const NodeBox* node_box, int num_nodes, int num_movable,
                        float bin_w, float bin_h, float target_density,
                        coord_t* node_grad, const char* xclbin_path) {
    const int    N          = DENSITY_GRID;
    const size_t box_bytes  = (size_t)num_nodes    * sizeof(NodeBox);
    const size_t mat_bytes  = (size_t)N * N        * sizeof(float);
    const size_t grad_bytes = (size_t)num_movable  * sizeof(coord_t);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::graph  fft(device, uuid, "density_fft_graph");

    xrt::bo d_node = xrt::bo(device, sizeof(coord_t), top.group_id(0));
    xrt::bo d_nptr = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut  = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb   = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo bo_grad = xrt::bo(device, grad_bytes, top.group_id(7));
    xrt::bo bo_box  = xrt::bo(device, box_bytes,  top.group_id(8));
    xrt::bo bo_bd   = xrt::bo(device, mat_bytes,  top.group_id(9));   // rho, then eField_x
    xrt::bo bo_din  = xrt::bo(device, mat_bytes,  top.group_id(10));  // field in/out, then eField_y
    xrt::bo bo_dout = xrt::bo(device, mat_bytes,  top.group_id(11));  // field out

    std::memcpy(bo_box.map<void*>(), node_box, box_bytes);
    bo_box.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // ---- density_bin: node_box -> bin_density (bo_bd) = rho ----
    {
        xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, bo_grad,
                           bo_box, bo_bd, bo_din, bo_dout,
                           0.0f, 0.0f, 0, 0, num_movable, 0,
                           num_nodes, bin_w, bin_h, target_density,
                           0, 0, (int)MODE_DENSITY_BIN);
        run.wait();
    }
    bo_bd.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::vector<float> rho((size_t)N * N);
    std::memcpy(rho.data(), bo_bd.map<void*>(), mat_bytes);

    // ---- field solve: rho -> Ex, Ey (crossing via bo_din -> bo_dout, scratch via host) ----
    auto field_pass = [&](const float* src, float* dst, int mode, int sub) {
        std::memcpy(bo_din.map<void*>(), src, mat_bytes);
        bo_din.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        if (mode == (int)MODE_DCT_TRANSPOSE) fft.run(N / DENSITY_LANES);
        xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, bo_grad,
                           bo_box, bo_bd, bo_din, bo_dout,
                           0.0f, 0.0f, 0, 0, 0, 0,
                           0, 1.0f, 1.0f, 1.0f,
                           sub, N, mode);
        run.wait();
        if (mode == (int)MODE_DCT_TRANSPOSE) fft.wait();
        bo_dout.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(dst, bo_dout.map<void*>(), mat_bytes);
    };

    std::vector<float> t1((size_t)N*N), a_uv((size_t)N*N), Ex_hat((size_t)N*N), Ey_hat((size_t)N*N);
    std::vector<float> tE((size_t)N*N), tY((size_t)N*N), Ex((size_t)N*N), Ey((size_t)N*N);
    field_pass(rho.data(),    t1.data(),     (int)MODE_DCT_TRANSPOSE, (int)TFH_DCT);
    field_pass(t1.data(),     a_uv.data(),   (int)MODE_DCT_TRANSPOSE, (int)TFH_DCT);
    field_pass(a_uv.data(),   Ex_hat.data(), (int)MODE_SPECTRAL, 0);
    field_pass(a_uv.data(),   Ey_hat.data(), (int)MODE_SPECTRAL, 1);
    field_pass(Ex_hat.data(), tE.data(),     (int)MODE_DCT_TRANSPOSE, (int)TFH_IDCT);
    field_pass(tE.data(),     Ex.data(),     (int)MODE_DCT_TRANSPOSE, (int)TFH_IDXST);
    field_pass(Ey_hat.data(), tY.data(),     (int)MODE_DCT_TRANSPOSE, (int)TFH_IDXST);
    field_pass(tY.data(),     Ey.data(),     (int)MODE_DCT_TRANSPOSE, (int)TFH_IDCT);

    // ---- force_gather: eField_x = bo_bd(9), eField_y = bo_din(10), node_box(8) -> node_grad(7) ----
    std::memcpy(bo_bd.map<void*>(),  Ex.data(), mat_bytes);
    std::memcpy(bo_din.map<void*>(), Ey.data(), mat_bytes);
    bo_bd.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_din.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    {
        xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, bo_grad,
                           bo_box, bo_bd, bo_din, bo_dout,
                           0.0f, 0.0f, 0, 0, num_movable, 0,
                           num_nodes, bin_w, bin_h, 0.0f,
                           0, 0, (int)MODE_FORCE_GATHER);
        run.wait();
    }
    bo_grad.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(node_grad, bo_grad.map<void*>(), grad_bytes);
}

// Stage 5c: one Nesterov step. Port aliasing (host_interface.hpp MODE_ITERATION_UPDATE):
// u_k=node_pos(0), precond=exp_lut(4), g_hpwl=node_grad(7), node_box(8), v_out=bin_density(9),
// g_density=dct_in(10), u_out=dct_out(11). Pure PL (no AIE graph).
void runIterUpdate(int num_movable,
                   const coord_t* g_hpwl, const coord_t* g_density,
                   const NodeBox* node_box, const coord_t* u_k, const float* precond,
                   float lambda, float alpha, float coeff, float die_xmax, float die_ymax,
                   coord_t* u_out, coord_t* v_out, const char* xclbin_path) {
    const int    M           = num_movable;
    const size_t coord_bytes = (size_t)M * sizeof(coord_t);
    const size_t box_bytes   = (size_t)M * sizeof(NodeBox);
    const size_t prec_bytes  = (size_t)M * sizeof(float);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");

    xrt::bo bo_uk   = xrt::bo(device, coord_bytes, top.group_id(0));   // u_k
    xrt::bo d_nptr  = xrt::bo(device, sizeof(int32_t), top.group_id(1));
    xrt::bo d_pins  = xrt::bo(device, sizeof(NodePin), top.group_id(2));
    xrt::bo d_npin  = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo bo_prec = xrt::bo(device, prec_bytes,  top.group_id(4));   // precond
    xrt::bo d_bb    = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums  = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo bo_ghp  = xrt::bo(device, coord_bytes, top.group_id(7));   // g_hpwl
    xrt::bo bo_box  = xrt::bo(device, box_bytes,   top.group_id(8));   // v_k + size
    xrt::bo bo_vout = xrt::bo(device, coord_bytes, top.group_id(9));   // v_{k+1} out
    xrt::bo bo_gden = xrt::bo(device, coord_bytes, top.group_id(10));  // g_density
    xrt::bo bo_uout = xrt::bo(device, coord_bytes, top.group_id(11));  // u_{k+1} out

    std::memcpy(bo_uk.map<void*>(),   u_k,       coord_bytes);
    std::memcpy(bo_prec.map<void*>(), precond,   prec_bytes);
    std::memcpy(bo_ghp.map<void*>(),  g_hpwl,    coord_bytes);
    std::memcpy(bo_box.map<void*>(),  node_box,  box_bytes);
    std::memcpy(bo_gden.map<void*>(), g_density, coord_bytes);
    bo_uk.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_prec.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_ghp.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_box.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_gden.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    xrt::run run = top(bo_uk, d_nptr, d_pins, d_npin, bo_prec, d_bb, d_sums, bo_ghp,
                       bo_box, bo_vout, bo_gden, bo_uout,
                       lambda, alpha, 0, 0, M, 0,             // inv_gamma=lambda, inv_lut_step=alpha, num_movable=M
                       0, coeff, die_xmax, die_ymax,          // bin_w=coeff, bin_h=die_xmax, target_density=die_ymax
                       0, 0, (int)MODE_ITERATION_UPDATE);
    run.wait();

    bo_uout.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    bo_vout.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(u_out, bo_uout.map<void*>(), coord_bytes);
    std::memcpy(v_out, bo_vout.map<void*>(), coord_bytes);
}

// Stage 5c: metrics reduce. Ports: node_pos(0), net_ptr(1), pins(2), bin_density=rho(9),
// out=dct_out(11). Pure PL (no AIE graph).
void runMetrics(const coord_t* node_pos, const int* net_ptr, const NodePin* pins,
                int num_nodes, int num_nets, int num_pins,
                const float* bin_density, float target_density,
                float* out_hpwl, float* out_overflow_sum, const char* xclbin_path) {
    const size_t node_bytes = (size_t)num_nodes    * sizeof(coord_t);
    const size_t nptr_bytes = (size_t)(num_nets+1) * sizeof(int32_t);
    const size_t pins_bytes = (size_t)num_pins     * sizeof(NodePin);
    const size_t bd_bytes   = (size_t)DENSITY_NBINS * sizeof(float);
    const size_t out_bytes  = 2 * sizeof(float);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");

    xrt::bo bo_node = xrt::bo(device, node_bytes, top.group_id(0));
    xrt::bo bo_nptr = xrt::bo(device, nptr_bytes, top.group_id(1));
    xrt::bo bo_pins = xrt::bo(device, pins_bytes, top.group_id(2));
    xrt::bo d_npin  = xrt::bo(device, sizeof(NodePin), top.group_id(3));
    xrt::bo d_lut   = xrt::bo(device, sizeof(float),   top.group_id(4));
    xrt::bo d_bb    = xrt::bo(device, sizeof(NetBBox), top.group_id(5));
    xrt::bo d_sums  = xrt::bo(device, sizeof(NetSums), top.group_id(6));
    xrt::bo d_grad  = xrt::bo(device, sizeof(coord_t), top.group_id(7));
    xrt::bo d_box   = xrt::bo(device, sizeof(NodeBox), top.group_id(8));
    xrt::bo bo_bd   = xrt::bo(device, bd_bytes,  top.group_id(9));
    xrt::bo d_din   = xrt::bo(device, sizeof(float), top.group_id(10));
    xrt::bo bo_out  = xrt::bo(device, out_bytes, top.group_id(11));

    std::memcpy(bo_node.map<void*>(), node_pos,    node_bytes);
    std::memcpy(bo_nptr.map<void*>(), net_ptr,     nptr_bytes);
    std::memcpy(bo_pins.map<void*>(), pins,        pins_bytes);
    std::memcpy(bo_bd.map<void*>(),   bin_density, bd_bytes);
    bo_node.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_nptr.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_pins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_bd.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    xrt::run run = top(bo_node, bo_nptr, bo_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                       d_box, bo_bd, d_din, bo_out,
                       0.0f, 0.0f, 0, num_nets, 0, 0,         // num_nets used
                       0, 1.0f, 1.0f, target_density,         // target_density used
                       0, 0, (int)MODE_METRICS);
    run.wait();

    bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    const float* o = bo_out.map<float*>();
    *out_hpwl          = o[0];
    *out_overflow_sum  = o[1];
}

// Stage 5c.5: the full placement loop in one device/graph session. See Driver.hpp.
int runPlacement(const PlacementConfig& cfg,
                 int num_nodes, int num_movable, int num_nets, int num_pins, int num_npins,
                 const coord_t* node_pos_init, const NodeBox* node_box_init,
                 const int* net_ptr, const NodePin* pins, const NodePin* npins,
                 const float* exp_lut, int lut_size,
                 const int* degree, const float* area, float avg_area,
                 float* out_hpwl_hist, float* out_ovfl_hist, coord_t* out_final_pos,
                 const char* xclbin_path) {
    const int    N   = num_nodes, M = num_movable, NBINS = DENSITY_NBINS;
    const int    G   = DENSITY_GRID;
    const float  bin_area = cfg.bin_w * cfg.bin_h;
    float total_movable_area = 0.0f;
    for (int n = 0; n < M; n++) total_movable_area += area[n];

    const size_t coordN = (size_t)N * sizeof(coord_t);
    const size_t coordM = (size_t)M * sizeof(coord_t);
    const size_t matB   = (size_t)NBINS * sizeof(float);      // 4 MB matrices
    const size_t big    = matB > coordM ? matB : coordM;      // gmem9/10/11 max use
    const size_t lutB   = (size_t)lut_size * sizeof(float);
    const size_t lp4    = lutB > coordM ? lutB : coordM;      // gmem4: exp_lut or precond

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::graph  fft(device, uuid, "density_fft_graph");

    xrt::bo b_np   = xrt::bo(device, coordN, top.group_id(0));   // node_pos (v) / u_k
    xrt::bo b_ptr  = xrt::bo(device, (size_t)(num_nets+1)*sizeof(int32_t), top.group_id(1));
    xrt::bo b_pin  = xrt::bo(device, (size_t)num_pins*sizeof(NodePin), top.group_id(2));
    xrt::bo b_npin = xrt::bo(device, (size_t)(num_npins>0?num_npins:1)*sizeof(NodePin), top.group_id(3));
    xrt::bo b_lut  = xrt::bo(device, lp4, top.group_id(4));      // exp_lut / precond
    xrt::bo b_bb   = xrt::bo(device, (size_t)num_nets*sizeof(NetBBox), top.group_id(5));
    xrt::bo b_sums = xrt::bo(device, (size_t)num_nets*sizeof(NetSums), top.group_id(6));
    xrt::bo b_grad = xrt::bo(device, coordM, top.group_id(7));   // g_hpwl / g_density / node_grad
    xrt::bo b_box  = xrt::bo(device, (size_t)N*sizeof(NodeBox), top.group_id(8));
    xrt::bo b_bd   = xrt::bo(device, big, top.group_id(9));      // rho / Ex / v_out
    xrt::bo b_din  = xrt::bo(device, big, top.group_id(10));     // field / Ey / g_density
    xrt::bo b_dout = xrt::bo(device, big, top.group_id(11));     // field / u_out

    // Static uploads (never change across iterations).
    std::memcpy(b_ptr.map<void*>(),  net_ptr, (size_t)(num_nets+1)*sizeof(int32_t));
    std::memcpy(b_pin.map<void*>(),  pins,    (size_t)num_pins*sizeof(NodePin));
    if (num_npins > 0) std::memcpy(b_npin.map<void*>(), npins, (size_t)num_npins*sizeof(NodePin));
    b_ptr.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    b_pin.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    if (num_npins > 0) b_npin.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // Host-side working state.
    std::vector<coord_t> node_pos(node_pos_init, node_pos_init + N);   // [N]; movable prefix = v
    std::vector<NodeBox> node_box(node_box_init, node_box_init + N);   // [N]; {x,y}=v, {w,h}=size
    std::vector<coord_t> u(M), v(M), v_prev(M), u_out(M), v_out(M);
    std::vector<coord_t> g_hpwl(M), g_density(M), gtot(M), gtot_prev(M);
    std::vector<float>   precond(M, 1.0f), rho((size_t)NBINS);  // precond OFF (sw_only default) -> weight 1
    std::vector<float>   t1((size_t)NBINS), a_uv((size_t)NBINS), Exh((size_t)NBINS), Eyh((size_t)NBINS);
    std::vector<float>   tE((size_t)NBINS), tY((size_t)NBINS), Ex((size_t)NBINS), Ey((size_t)NBINS);
    for (int n = 0; n < M; n++) { u[n] = node_pos_init[n]; v[n] = node_pos_init[n]; }

    float gamma          = cfg.gamma_schedule ? 10.0f * cfg.base_gamma : cfg.base_gamma;
    float lambda         = 1.0f;
    float nesterov_ak    = 1.0f;
    bool  have_prev      = false;
    float prev_hpwl      = 0.0f;   // for the density-weight trend
    int   conv_remaining = -1;     // overflow-below-stop countdown (-1 until first crossing)

    // Preconditioner (sw_only updatePrecondWeights + auto-enable, faithful port). Auto-ON iff the
    // design has movable macros (die-relative 0.02% area threshold, matching sw_only #5); an explicit
    // cfg.enable_preconditioning (0/1) overrides. precond_coef starts at 1 and doubles every 20 iters
    // once overflow<0.3 (sw_only escalation). Weights use RAW area (avg_area=1 => precond_raw_area=true),
    // matching sw_only's MMS default. When OFF, precond stays 1 (no-op divide in iteration_update).
    const float macro_area_thresh = 0.0002f * cfg.die_x * cfg.die_y;
    int num_mov_macros = 0;
    for (int n = 0; n < M; n++) if (area[n] > macro_area_thresh) num_mov_macros++;
    const bool precond_on = (cfg.enable_preconditioning >= 0)
        ? (cfg.enable_preconditioning != 0) : (num_mov_macros > 0);
    float precond_coef = 1.0f;
    printf("[place] preconditioner %s (%d movable macros detected)\n",
           precond_on ? "ON" : "OFF", num_mov_macros);

    // One fused transform/spectral pass: src -> dst (scratch crosses via host, like runField).
    auto field_pass = [&](const float* src, float* dst, int mode, int sub) {
        std::memcpy(b_din.map<void*>(), src, matB);
        b_din.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        if (mode == (int)MODE_DCT_TRANSPOSE) fft.run(G / DENSITY_LANES);
        xrt::run run = top(b_np, b_ptr, b_pin, b_npin, b_lut, b_bb, b_sums, b_grad,
                           b_box, b_bd, b_din, b_dout,
                           0.0f, 0.0f, 0, 0, 0, 0, 0, 1.0f, 1.0f, 1.0f, sub, G, mode);
        run.wait();
        if (mode == (int)MODE_DCT_TRANSPOSE) fft.wait();
        b_dout.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(dst, b_dout.map<void*>(), matB);
    };

    int iters_run = 0;
    for (int iter = 1; iter <= cfg.max_iters; iter++) {
        const float inv_gamma    = 1.0f / gamma;
        const float inv_lut_step = 1.0f / (PLACE_STEP_NORM * gamma);

        // ---- push v into the movable prefix of node_pos and node_box ----
        for (int n = 0; n < M; n++) {
            node_pos[n] = v[n];
            node_box[n].x = v[n].x; node_box[n].y = v[n].y;
        }
        std::memcpy(b_np.map<void*>(),  node_pos.data(), coordN);
        std::memcpy(b_box.map<void*>(), node_box.data(), (size_t)N*sizeof(NodeBox));
        std::memcpy(b_lut.map<void*>(), exp_lut, lutB);
        b_np.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        b_box.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        b_lut.sync(XCL_BO_SYNC_BO_TO_DEVICE);

        // ---- HPWL gradient at v -> b_grad, read to host ----
        { xrt::run r = top(b_np, b_ptr, b_pin, b_npin, b_lut, b_bb, b_sums, b_grad,
                           b_box, b_bd, b_din, b_dout,
                           inv_gamma, inv_lut_step, lut_size, num_nets, M, num_npins,
                           N, cfg.bin_w, cfg.bin_h, cfg.target_density, 0, 0, (int)MODE_HPWL_GRAD);
          r.wait(); }
        b_grad.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(g_hpwl.data(), b_grad.map<void*>(), coordM);

        // ---- density gradient at v: density_bin -> rho, field solve, force_gather -> g_density ----
        { xrt::run r = top(b_np, b_ptr, b_pin, b_npin, b_lut, b_bb, b_sums, b_grad,
                           b_box, b_bd, b_din, b_dout,
                           0.0f, 0.0f, 0, 0, M, 0, N, cfg.bin_w, cfg.bin_h, cfg.target_density,
                           0, 0, (int)MODE_DENSITY_BIN);
          r.wait(); }
        b_bd.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(rho.data(), b_bd.map<void*>(), matB);            // rho at v (for overflow)

        field_pass(rho.data(), t1.data(),  (int)MODE_DCT_TRANSPOSE, (int)TFH_DCT);
        field_pass(t1.data(),  a_uv.data(),(int)MODE_DCT_TRANSPOSE, (int)TFH_DCT);
        field_pass(a_uv.data(), Exh.data(),(int)MODE_SPECTRAL, 0);
        field_pass(a_uv.data(), Eyh.data(),(int)MODE_SPECTRAL, 1);
        field_pass(Exh.data(), tE.data(),  (int)MODE_DCT_TRANSPOSE, (int)TFH_IDCT);
        field_pass(tE.data(),  Ex.data(),  (int)MODE_DCT_TRANSPOSE, (int)TFH_IDXST);
        field_pass(Eyh.data(), tY.data(),  (int)MODE_DCT_TRANSPOSE, (int)TFH_IDXST);
        field_pass(tY.data(),  Ey.data(),  (int)MODE_DCT_TRANSPOSE, (int)TFH_IDCT);

        std::memcpy(b_bd.map<void*>(),  Ex.data(), matB);            // eField_x = gmem9
        std::memcpy(b_din.map<void*>(), Ey.data(), matB);            // eField_y = gmem10
        b_bd.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        b_din.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        { xrt::run r = top(b_np, b_ptr, b_pin, b_npin, b_lut, b_bb, b_sums, b_grad,
                           b_box, b_bd, b_din, b_dout,
                           0.0f, 0.0f, 0, 0, M, 0, N, cfg.bin_w, cfg.bin_h, 0.0f,
                           0, 0, (int)MODE_FORCE_GATHER);
          r.wait(); }
        b_grad.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        std::memcpy(g_density.data(), b_grad.map<void*>(), coordM);

        // ---- host metrics (verified PL metrics module replicated on host to save a pass) ----
        const double hpwl = hostHPWL(node_pos.data(), net_ptr, pins, num_nets);
        const double overflow = hostOverflow(rho.data(), NBINS, cfg.target_density,
                                             bin_area, total_movable_area);
        out_hpwl_hist[iter-1] = (float)hpwl;
        out_ovfl_hist[iter-1] = (float)overflow;

        // ---- host policy ----
        if (iter == 1)
            lambda = initDensityWeight(g_hpwl.data(), g_density.data(), M,
                                       cfg.density_weight_init_multiplier);
        for (int n = 0; n < M; n++) {
            gtot[n].x = g_hpwl[n].x - lambda * g_density[n].x;
            gtot[n].y = g_hpwl[n].y - lambda * g_density[n].y;
        }
        float alpha = have_prev
            ? bbStepLength(v.data(), v_prev.data(), gtot.data(), gtot_prev.data(), M)
            : cfg.init_step_length;
        // Preconditioner weights for this iteration: w = max(1, degree + precond_coef*lambda*area)
        // (raw area => avg_area=1). iteration_update divides the combined gradient by w. Essential for
        // movable-macro (MMS) convergence; with dff_force_ratio the schedule's dff still comes from the
        // gradient L1 norms, not precond mass, so leaving precond=1 when OFF is a clean no-op.
        if (precond_on)
            updatePrecondWeights(precond.data(), degree, area, M, /*avg_area=*/1.0f, precond_coef, lambda);
        const float coeff = momentumCoeff(nesterov_ak, cfg.enable_momentum);

        printf("[place] iter %d: HPWL=%.6g overflow=%.4f lambda=%.4g alpha=%.4g coeff=%.4f gamma=%.4g\n",
               iter, hpwl, overflow, lambda, alpha, coeff, gamma);

        // ---- iteration_update: one Nesterov step -> u_{k+1}, v_{k+1} ----
        std::memcpy(b_np.map<void*>(),   u.data(),         coordM);   // u_k          (gmem0)
        std::memcpy(b_lut.map<void*>(),  precond.data(),   (size_t)M*sizeof(float)); // precond (gmem4)
        std::memcpy(b_grad.map<void*>(), g_hpwl.data(),    coordM);   // g_hpwl       (gmem7)
        std::memcpy(b_din.map<void*>(),  g_density.data(), coordM);   // g_density    (gmem10)
        b_np.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        b_lut.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        b_grad.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        b_din.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        { xrt::run r = top(b_np, b_ptr, b_pin, b_npin, b_lut, b_bb, b_sums, b_grad,
                           b_box, b_bd, b_din, b_dout,
                           lambda, alpha, 0, 0, M, 0, 0, coeff, cfg.die_x, cfg.die_y,
                           0, 0, (int)MODE_ITERATION_UPDATE);
          r.wait(); }
        b_dout.sync(XCL_BO_SYNC_BO_FROM_DEVICE);   // u_{k+1}
        b_bd.sync(XCL_BO_SYNC_BO_FROM_DEVICE);     // v_{k+1}
        std::memcpy(u_out.data(), b_dout.map<void*>(), coordM);
        std::memcpy(v_out.data(), b_bd.map<void*>(),   coordM);

        // ---- advance: save prev for BB, commit u/v ----
        v_prev = v; gtot_prev = gtot; have_prev = true;
        u = u_out; v = v_out;

        // ---- schedule updates for the NEXT iteration (sw_only performIteration) ----
        // λ and γ share one skip_update gate (freeze both on 2 of 3 early / mid-balance iters).
        // dff = density-force fraction from this iteration's gradients (dff_force_ratio form).
        const float dff  = densityForceFraction(g_hpwl.data(), g_density.data(), M, lambda);
        const bool  skip = scheduleSkipUpdate(iter, dff);
        if (!skip) {
            lambda = updateDensityWeight(lambda, (float)hpwl, prev_hpwl, iter,
                                         cfg.density_weight_min_step, cfg.density_weight_max_step);
            if (cfg.gamma_schedule) gamma = updateGammaValue((float)overflow, cfg.base_gamma);
            // sw_only precond escalation: double precond_coef every 20 iters once overflow < 0.3
            // (progressively tightens macro damping in late placement; cap 1024).
            if (precond_on && overflow < 0.3f && precond_coef < 1024.0f && iter % 20 == 0)
                precond_coef *= 2.0f;
        }
        prev_hpwl = (float)hpwl;   // trend reference for the next iteration (updated every iter)
        iters_run = iter;

        // ---- convergence: stop once overflow holds below the stop threshold for conv_iters ----
        // (sw_only checkConvergence overflow countdown; full divergence/best guards TODO on device).
        if (iter >= cfg.min_iters) {
            if (overflow < cfg.overflow_threshold) {
                if (conv_remaining < 0) conv_remaining = cfg.conv_iters;
                if (--conv_remaining <= 0) { printf("[place] converged at iter %d (overflow %.4f)\n",
                                                    iter, overflow); break; }
            } else if (conv_remaining >= 0) {
                conv_remaining = -1;   // rose back above the threshold; reset the countdown
            }
        }
    }

    for (int n = 0; n < M; n++) out_final_pos[n] = u[n];
    return iters_run;
}

} // namespace plalgo
