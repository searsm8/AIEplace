// HpwlGradGraph.h
// ADF graph computing HPWL partial derivatives via the hpwl_grad kernel using
// stream PLIO. Ported from markv1 partials/PartialsGraph.h (renamed). Bring-up
// uses a single instance; HPWL_GRAD_INSTANCES self-defaults so the shared
// Makefile need not be touched yet.
#pragma once
#include "kernels.h"
#include <string>

#ifndef HPWL_GRAD_INSTANCES
#define HPWL_GRAD_INSTANCES 1
#endif

#define FIFO_SIZE 7000 // deep FIFO lets the PL burst many net groups without stalling

class HpwlGradGraph : public adf::graph {
private:
  adf::kernel my_hpwl_grad_kernel[HPWL_GRAD_INSTANCES];
public:
  adf::input_plio  x_in[HPWL_GRAD_INSTANCES];   // pin-coordinate packet in
  adf::output_plio grad_out[HPWL_GRAD_INSTANCES]; // per-pin dW/dx,dW/dy out

  HpwlGradGraph() {
    for (int i = 0; i < HPWL_GRAD_INSTANCES; i++) {
      my_hpwl_grad_kernel[i] = adf::kernel::create(hpwl_grad_kernel);

      // PLIO ports. The .dat paths are only used by standalone x86sim/aiesim;
      // in sw_emu/hw these ports are driven by the PL data movers (link.cfg).
      x_in[i] = adf::input_plio::create(
          "hpwl_grad_x_in_" + std::to_string(i), adf::plio_128_bits,
          "golden_data/hpwl_grad/x_in" + std::to_string(i) + ".dat");
      grad_out[i] = adf::output_plio::create(
          "hpwl_grad_out_" + std::to_string(i), adf::plio_128_bits,
          "simdata/hpwl_grad" + std::to_string(i) + ".dat");

      adf::connect<adf::stream> net_in(x_in[i].out[0], my_hpwl_grad_kernel[i].in[0]);
      adf::fifo_depth(net_in) = FIFO_SIZE;

      adf::connect<adf::stream> net_out(my_hpwl_grad_kernel[i].out[0], grad_out[i].in[0]);
      adf::fifo_depth(net_out) = FIFO_SIZE;

      adf::source(my_hpwl_grad_kernel[i]) = "hpwl_grad/hpwl_grad_kernel.cpp";
      adf::runtime<adf::ratio>(my_hpwl_grad_kernel[i]) = 0.9;
    }
  }
};
