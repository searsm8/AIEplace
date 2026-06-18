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

} // namespace plalgo

#endif // PL_ALGO_DRIVER_HPP
