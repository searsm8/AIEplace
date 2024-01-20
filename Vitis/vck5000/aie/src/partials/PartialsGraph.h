// partialsGraph.h
// implements a graph to compute hpwl partial derivatives using kernels with STREAMS
#pragma once
#include "partials_kernels.h"

#define DEBUG_OUTPUT // output files for a+, a-, b+, b-, c+, c-

class PartialsGraph : public adf::graph {
private:
  adf::kernel abc_kernel; // a plus/minus
  adf::kernel partials_kernel;
public:
  adf::input_plio x_in;

  PartialsGraph(){
    abc_kernel = adf::kernel::create(compute_abc);
    partials_kernel = adf::kernel::create(compute_partials);

    // Primary inputs to the AIE array
    x_in = adf::input_plio::create("x_in", adf::plio_32_bits, "golden_data/partials/x_in.dat");

    // Input connections for abc_kernel
    adf::connect<adf::stream>(x_in.out[0], abc_kernel.in[0]); // x-coords

    // Input connections for partials_kernel
    adf::connect<adf::stream> net_xa(abc_kernel.out[0], partials_kernel.in[0]);
    adf::connect<adf::stream> net_bc(abc_kernel.out[1], partials_kernel.in[1]);
    adf::fifo_depth(net_xa) = 92; // 92 is large enough to handle nets of size 7 (with minor stalls)
                                  // or nets of size 6 without stalls
                                  // add or subtract 24 to the buffer size to increase net size 
                                  
                                  // nets larger than 7 should be handled by the host code
    //adf::fifo_depth(net_bc) = 96*2; // this one doesn't need a fifo

#ifdef DEBUG_OUTPUT
    // Optional outputs for debugging intermediate terms
    adf::output_plio outplio_xa, outplio_bc, outplio_partials;

    outplio_xa = adf::output_plio::create("outplio_xa" , adf::plio_32_bits, "simdata/xa.dat");
    outplio_bc = adf::output_plio::create("outplio_bc", adf::plio_32_bits, "simdata/bc.dat");
    outplio_partials = adf::output_plio::create("outplio_partials", adf::plio_32_bits, "simdata/partials.dat");

    // Connections for debugging terms
    adf::connect<adf::stream>(abc_kernel.out[0], outplio_xa.in[0]);
    adf::connect<adf::stream>(abc_kernel.out[1], outplio_bc.in[0]);
    adf::connect<adf::stream>(partials_kernel.out[0], outplio_partials.in[0]);
#endif


    adf::source(abc_kernel) = "compute_abc.cpp";
    adf::runtime<ratio>(abc_kernel) = 1;

    adf::source(partials_kernel) = "compute_partials.cpp";
    adf::runtime<ratio>(partials_kernel) = 1;
  }
};
