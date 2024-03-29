// partialsGraph.h
// implements a graph to compute hpwl partial derivatives using kernels with STREAMS
#pragma once
#include "partials_kernels.h"

#define DEBUG_OUTPUT // output files for a+, a-, b+, b-, c+, c-
#define NUM_GRAPHS 1 // TODO: combine with Common.h definition
#define str std::to_string

class PartialsGraph : public adf::graph {
private:
  adf::kernel abc_kernel[NUM_GRAPHS]; // a plus/minus
  adf::kernel partials_kernel[NUM_GRAPHS];
public:
  adf::input_plio x_in[NUM_GRAPHS];
  adf::output_plio outplio_partials[NUM_GRAPHS];

  PartialsGraph(){
    for(int i = 0; i < NUM_GRAPHS; i++)
    {
      abc_kernel[i] = adf::kernel::create(compute_abc);
      partials_kernel[i] = adf::kernel::create(compute_partials);

      // Primary inputs to the AIE array
      x_in[i] = adf::input_plio::create("x_in_"+str(i), adf::plio_128_bits, "golden_data/partials/x_in"+str(i)+".dat");
      outplio_partials[i] = adf::output_plio::create("outplio_partials_"+str(i), adf::plio_128_bits, "simdata/partials"+str(i)+".dat");

      // Input connections for abc_kernel
      adf::connect<adf::stream>(x_in[i].out[0], abc_kernel[i].in[0]); // x-coords

      // Input connections for partials_kernel
      adf::connect<adf::stream> net_xa(abc_kernel[i].out[0], partials_kernel[i].in[0]);
      adf::connect<adf::stream> net_bc(abc_kernel[i].out[1], partials_kernel[i].in[1]);
      adf::fifo_depth(net_xa) = 140; // 140 is large enough to handle nets of size 8 without stalls
                                    // or nets of size 9 (with minor stalls)
                                    // add or subtract 24 to the buffer size to increase net size 
                                    // nets larger than 8 should be handled by the host code
      //adf::fifo_depth(net_bc) = 96*2; // this one doesn't need a fifo

      adf::connect<adf::stream>(partials_kernel[i].out[0], outplio_partials[i].in[0]);

  #ifdef DEBUG_OUTPUT
      // Optional outputs for debugging intermediate terms
      //adf::output_plio outplio_xa[NUM_GRAPHS], outplio_bc[NUM_GRAPHS];
      //outplio_xa[i] = adf::output_plio::create("outplio_xa"+str(i), adf::plio_32_bits, "simdata/xa"+str(i)+".dat");
      //outplio_bc[i] = adf::output_plio::create("outplio_bc"+str(i), adf::plio_32_bits, "simdata/bc"+str(i)+".dat");
      
      // Connections for debugging terms
      //adf::connect<adf::stream>(abc_kernel[i].out[0], outplio_xa[i].in[0]);
      //adf::connect<adf::stream>(abc_kernel[i].out[1], outplio_bc[i].in[0]);
  #endif



      adf::source(abc_kernel[i]) = "compute_abc.cpp";
      adf::runtime<ratio>(abc_kernel[i]) = 1;

      adf::source(partials_kernel[i]) = "compute_partials.cpp";
      adf::runtime<ratio>(partials_kernel[i]) = 1;
    }
  }
};
