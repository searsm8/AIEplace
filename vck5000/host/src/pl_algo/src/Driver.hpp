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

// Milestone B: run a single HPWL-gradient packet through the AIE graph.
//   in/out are flat float arrays already in the AIE packet format
//   (host_interface.hpp): `in` holds the control beat + sorted, SIMD-grouped pin
//   coords; `out` receives the per-pin partials. Both lengths must be multiples
//   of FLOATS_PER_BEAT (4). Drives top (DDR<->stream pass-through) and the
//   hpwl_grad_graph. POD-only signature to keep the ABI boundary clean.
void runHpwlGradPacket(const float* in, int in_floats,
                       float* out, int out_floats,
                       const char* xclbin_path);

} // namespace plalgo

#endif // PL_ALGO_DRIVER_HPP
