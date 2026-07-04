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

#include <cstring>
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

} // namespace plalgo
