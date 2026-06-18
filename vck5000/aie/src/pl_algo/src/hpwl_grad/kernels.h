#ifndef HPWL_GRAD_KERNELS_H
#define HPWL_GRAD_KERNELS_H

// kernels.h -- pl_algo HPWL-gradient AIE kernel declarations.
// Ported from markv1 aie/src/markv1/src/partials/kernels.h (renamed "partials"
// -> "hpwl_grad"). The kernel math is unchanged and is the validated markv1
// weighted-average wirelength partials computation.

#include "system_settings.h"
#include <adf/x86sim/x86simDebug.h> // for printf debugging
using namespace adf;

// 1/gamma factor baked into the exponents. Must equal AIE_INV_GAMMA in the host
// contract (host_interface.hpp); runtime params are not available on the VCK5000.
constexpr float inv_gamma = 0.25;

// HPWL-gradient kernel: consumes a net-group packet (control beat + sorted,
// SIMD-grouped pin coordinates) and emits per-pin dW/dx, dW/dy. See
// host_interface.hpp "HPWL GRADIENT EXTENSION" for the packet layout.
void hpwl_grad_kernel(input_stream<float> * __restrict x_in,
                      output_stream<float> * __restrict grad_out);

#endif
