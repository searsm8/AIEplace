// partialsGraph.h
// implements a graph to compute hpwl partial derivatives using kernels with STREAMS
#pragma once
#include "partials_kernels.h"
#include "Common.h"

#define DEBUG_OUTPUT // output files for a+, a-, b+, b-, c+, c-
#define FIFO_SIZE 7000

class PartialsGraph : public adf::graph {
private:
  adf::kernel abc_kernel[PARTIALS_GRAPH_COUNT]; // a plus/minus
  adf::kernel partials_kernel[PARTIALS_GRAPH_COUNT];
public:
  adf::input_plio x_in[PARTIALS_GRAPH_COUNT];
  adf::output_plio outplio_partials[PARTIALS_GRAPH_COUNT];

  PartialsGraph(){
    for(int i = 0; i < PARTIALS_GRAPH_COUNT; i++)
    {
      abc_kernel[i] = adf::kernel::create(compute_abc);
      partials_kernel[i] = adf::kernel::create(compute_partials);

      // Primary inputs to the AIE array
      x_in[i] = adf::input_plio::create("x_in_"+std::to_string(i), adf::plio_128_bits, "golden_data/partials/x_in"+std::to_string(i)+".dat");
      outplio_partials[i] = adf::output_plio::create("outplio_partials_"+std::to_string(i), adf::plio_128_bits, "simdata/partials"+std::to_string(i)+".dat");

      // Input connections for abc_kernel
      adf::connect<adf::stream> net_in(x_in[i].out[0], abc_kernel[i].in[0]); // x-coords
      adf::fifo_depth(net_in) = FIFO_SIZE; // This FIFO allows the host to send data for many nets in bursts

      // Input connections for partials_kernel
      adf::connect<adf::stream> net_xa(abc_kernel[i].out[0], partials_kernel[i].in[0]);
      adf::connect<adf::stream> net_bc(abc_kernel[i].out[1], partials_kernel[i].in[1]);
      adf::fifo_depth(net_xa) = FIFO_SIZE; // 140 is large enough to handle nets of size 8 without stalls
                                          //  or nets of size 9 (with minor stalls)
                                         //   add or subtract 24 to the buffer size to increase net size 
                                        //    nets larger than 8 should be handled by the host code
      //adf::fifo_depth(net_bc) = FIFO_SIZE; // this one doesn't need a fifo

      adf::connect<adf::stream> net_out(partials_kernel[i].out[0], outplio_partials[i].in[0]);
      adf::fifo_depth(net_out) = FIFO_SIZE; // this one doesn't need a fifo

  #ifdef DEBUG_OUTPUT
      // Optional outputs for debugging intermediate terms
      //adf::output_plio outplio_xa[PARTIALS_GRAPH_COUNT], outplio_bc[PARTIALS_GRAPH_COUNT];
      //outplio_xa[i] = adf::output_plio::create("outplio_xa"+std::to_string(i), adf::plio_32_bits, "simdata/xa"+std::to_string(i)+".dat");
      //outplio_bc[i] = adf::output_plio::create("outplio_bc"+std::to_string(i), adf::plio_32_bits, "simdata/bc"+std::to_string(i)+".dat");
      
      // Connections for debugging terms
      //adf::connect<adf::stream>(abc_kernel[i].out[0], outplio_xa[i].in[0]);
      //adf::connect<adf::stream>(abc_kernel[i].out[1], outplio_bc[i].in[0]);
  #endif



      adf::source(abc_kernel[i]) = "compute_abc.cpp";
      adf::runtime<adf::ratio>(abc_kernel[i]) = 0.5;

      adf::source(partials_kernel[i]) = "compute_partials.cpp";
      adf::runtime<adf::ratio>(partials_kernel[i]) = 0.5;
    }
  }
};
