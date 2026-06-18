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
#include "experimental/xrt_graph.h"

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

void runHpwlGradPacket(const float* in, int in_floats,
                       float* out, int out_floats,
                       const char* xclbin_path) {
    const size_t in_bytes  = (size_t)in_floats  * sizeof(float);
    const size_t out_bytes = (size_t)out_floats * sizeof(float);

    // ---- open device, load xclbin, get the kernel and the AIE graph ----
    xrt::device device(0);
    xrt::uuid   uuid = device.load_xclbin(xclbin_path);
    xrt::kernel top(device, uuid, "top");
    xrt::graph  graph(device, uuid, "hpwl_grad_graph");

    // top args: 0=hpwl_packet (in), 1=hpwl_grad (out); the two axis stream ports
    // are wired to the AIE via link.cfg and are NOT XRT args.
    xrt::bo bo_in  = xrt::bo(device, in_bytes,  top.group_id(0));
    xrt::bo bo_out = xrt::bo(device, out_bytes, top.group_id(1));

    std::memcpy(bo_in.map<void*>(), in, in_bytes);
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // Start the AIE graph (one run consumes one packet), then run top to feed the
    // packet and drain the partials. in/out are sized in 128b beats (4 floats).
    graph.run(1);
    xrt::run run = top(bo_in, bo_out, in_floats / 4, out_floats / 4);
    run.wait();
    graph.wait();

    bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    std::memcpy(out, bo_out.map<void*>(), out_bytes);
}

} // namespace plalgo
