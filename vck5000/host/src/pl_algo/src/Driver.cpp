// Driver.cpp -- native XRT host driver for the v0 HPWL kernel. See Driver.hpp.
//
// The kernel signature (pl/src/pl_algo/src/top.cpp) is:
//     void top(const coord_t* node_pos,   // arg 0
//              const int*     net_ptr,    // arg 1
//              const PinRecord* pins,     // arg 2
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
    const size_t pins_bytes = pk.pins.size()     * sizeof(PinRecord);
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
    const size_t node_bytes = pk.node_pos.size() * sizeof(coord_t);
    const size_t nptr_bytes = pk.net_ptr.size()  * sizeof(int32_t);
    const size_t pins_bytes = pk.pins.size()     * sizeof(PinRecord);
    const size_t lut_bytes  = (size_t)lut_size   * sizeof(float);
    const size_t grad_bytes = (size_t)M          * sizeof(coord_t);

    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");

    // top args: 0=node_pos 1=net_ptr 2=pins 3=exp_lut 4=node_grad (then scalars).
    xrt::bo bo_node = xrt::bo(device, node_bytes, top.group_id(0));
    xrt::bo bo_nptr = xrt::bo(device, nptr_bytes, top.group_id(1));
    xrt::bo bo_pins = xrt::bo(device, pins_bytes, top.group_id(2));
    xrt::bo bo_lut  = xrt::bo(device, lut_bytes,  top.group_id(3));
    xrt::bo bo_grad = xrt::bo(device, grad_bytes, top.group_id(4));

    std::memcpy(bo_node.map<void*>(), pk.node_pos.data(), node_bytes);
    std::memcpy(bo_nptr.map<void*>(), pk.net_ptr.data(),  nptr_bytes);
    std::memcpy(bo_pins.map<void*>(), pk.pins.data(),     pins_bytes);
    std::memcpy(bo_lut.map<void*>(),  exp_lut,            lut_bytes);

    bo_node.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_nptr.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_pins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_lut.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    xrt::run run = top(bo_node, bo_nptr, bo_pins, bo_lut, bo_grad,
                       inv_gamma, inv_lut_step, lut_size, num_nets, M);
    run.wait();

    bo_grad.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(node_grad, bo_grad.map<void*>(), grad_bytes);
}

} // namespace plalgo
