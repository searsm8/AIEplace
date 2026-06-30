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

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"

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
    xrt::bo bo_box   = xrt::bo(device, sizeof(NodeBox), top.group_id(8)); // inert dummy
    xrt::bo bo_bd    = xrt::bo(device, sizeof(float),   top.group_id(9)); // inert dummy

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
                       bo_box, bo_bd,
                       inv_gamma, inv_lut_step, lut_size, num_nets, M, num_npins,
                       pk.header.num_nodes, 1.0f, 1.0f, 1.0f,   // density scalars (unused here)
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

    std::memcpy(bo_box.map<void*>(), pk.node_box.data(), box_bytes);
    bo_box.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    xrt::run run = top(d_node, d_nptr, d_pins, d_npin, d_lut, d_bb, d_sums, d_grad,
                       bo_box, bo_bd,
                       0.0f, 0.0f, 0, 0, M, 0,                 // HPWL scalars (unused here)
                       N, bin_w, bin_h, target_density,
                       (int)MODE_DENSITY_BIN);
    run.wait();

    bo_bd.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(bin_density, bo_bd.map<void*>(), bd_bytes);
}

} // namespace plalgo
