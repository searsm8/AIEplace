// partialsGraph.h
// implements a graph to compute hpwl partial derivatives using kernels with STREAMS
#pragma once
#include "partials_kernels.h"
#include "Common.h" // For PARTIALS_GRAPH_COUNT 

#define DEBUG_OUTPUT // output files for a+, a-, b+, b-, c+, c-
#define FIFO_SIZE 7000
#define PACKET_CLUSTER_SIZE 4 // each packet stream serves this many AIE compute units

class PartialsGraph : public adf::graph {
private:
  adf::kernel abc_kernel[PARTIALS_GRAPH_COUNT]; // a plus/minus
  adf::kernel partials_kernel[PARTIALS_GRAPH_COUNT];
public:
  adf::input_plio x_in[PARTIALS_GRAPH_COUNT];
  adf::output_plio outplio_partials[PARTIALS_GRAPH_COUNT];

  PartialsGraph(){
    int packet_stream_count = PARTIALS_GRAPH_COUNT / PACKET_CLUSTER_SIZE;
    assert(packet_stream_count * PACKET_CLUSTER_SIZE == PARTIALS_GRAPH_COUNT && "Invalid PARTIALS_GRAPH_COUNT");

    for(int i = 0; i < packet_stream_count; i++) // iterate over number of packet streams
    {
      // create packet splitter and merger for this packet stream
      adf::pktsplit<PACKET_CLUSTER_SIZE> splitter; // this may need to be an array outside loop/
      adf::pktmerge<PACKET_CLUSTER_SIZE> merger;

      splitter = adf::pktsplit<PACKET_CLUSTER_SIZE>::create();
      merger   = adf::pktmerge<PACKET_CLUSTER_SIZE>::create();

      for(int j = 0; j < PACKET_CLUSTER_SIZE; j++)
      {
        int index = i*PACKET_CLUSTER_SIZE + j;

        abc_kernel[index] = adf::kernel::create(compute_abc);
        partials_kernel[index] = adf::kernel::create(compute_partials);

        // Primary inputs to the AIE array
        x_in[index] = adf::input_plio::create("x_in_"+std::to_string(i), adf::plio_128_bits, "golden_data/partials/x_in"+std::to_string(i)+".dat");
        outplio_partials[index] = adf::output_plio::create("outplio_partials_"+std::to_string(i), adf::plio_128_bits, "simdata/partials"+std::to_string(i)+".dat");

        // Input connections for abc_kernel
        adf::connect<adf::stream> net_in(x_in[index].out[0], abc_kernel[index].in[0]); // x-coords
        adf::fifo_depth(net_in) = FIFO_SIZE; // This FIFO allows the host to send data for many nets in bursts

        // Input connections for partials_kernel
        adf::connect<adf::stream> net_xa(abc_kernel[index].out[0], partials_kernel[index].in[0]);
        adf::connect<adf::stream> net_bc(abc_kernel[index].out[1], partials_kernel[index].in[1]);
        adf::fifo_depth(net_xa) = FIFO_SIZE; // 140 is large enough to handle nets of size 8 without stalls
                                            //  or nets of size 9 (with minor stalls)
                                          //   add or subtract 24 to the buffer size to increase net size 
                                          //    nets larger than 8 should be handled by the host code
        //adf::fifo_depth(net_bc) = FIFO_SIZE; // this one doesn't need a fifo

        adf::connect<adf::stream> net_out(partials_kernel[index].out[0], outplio_partials[index].in[0]);
        adf::fifo_depth(net_out) = FIFO_SIZE; // this one doesn't need a fifo

        adf::source(abc_kernel[index]) = "compute_abc.cpp";
        adf::runtime<adf::ratio>(abc_kernel[index]) = 0.5;

        adf::source(partials_kernel[index]) = "compute_partials.cpp";
        adf::runtime<adf::ratio>(partials_kernel[index]) = 0.5;

        #ifdef DEBUG_OUTPUT
            // Optional outputs for debugging intermediate terms
            //adf::output_plio outplio_xa[PARTIALS_GRAPH_COUNT], outplio_bc[PARTIALS_GRAPH_COUNT];
            //outplio_xa[i] = adf::output_plio::create("outplio_xa"+std::to_string(i), adf::plio_32_bits, "simdata/xa"+std::to_string(i)+".dat");
            //outplio_bc[i] = adf::output_plio::create("outplio_bc"+std::to_string(i), adf::plio_32_bits, "simdata/bc"+std::to_string(i)+".dat");
            
            // Connections for debugging terms
            //adf::connect<adf::stream>(abc_kernel[i].out[0], outplio_xa[i].in[0]);
            //adf::connect<adf::stream>(abc_kernel[i].out[1], outplio_bc[i].in[0]);
        #endif
      }
    }
  }
};
