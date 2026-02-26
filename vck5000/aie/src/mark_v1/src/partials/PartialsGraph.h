// partialsGraph.h
// implements a graph to compute hpwl partial derivatives using kernels with STREAMS
#pragma once
#include "kernels.h"
#include "Common.h" // For PARTIALS_GRAPH_COUNT 

//#define DEBUG_OUTPUT // output files for a+, a-, b+, b-, c+, c-
#define FIFO_SIZE 7000
#define PACKET_CLUSTER_SIZE 1 // each packet stream serves this many AIE compute units

class PartialsGraph : public adf::graph {
private:
  adf::kernel my_abc_kernel[PARTIALS_GRAPH_COUNT]; // a plus/minus
  adf::kernel my_partials_kernel[PARTIALS_GRAPH_COUNT];
public:
  adf::input_plio x_in[PARTIALS_GRAPH_COUNT];
  adf::output_plio outplio_partials[PARTIALS_GRAPH_COUNT];

  PartialsGraph(){
    // create a packet splitter/merger for every `4` streams going out
    int packet_stream_count = PARTIALS_GRAPH_COUNT / PACKET_CLUSTER_SIZE;
    assert(packet_stream_count * PACKET_CLUSTER_SIZE == PARTIALS_GRAPH_COUNT && "PARTIALS_GRAPH_COUNT should be divisible by 4");

    for(int i = 0; i < packet_stream_count; i++) // iterate over number of packet streams
    {
      // create packet splitter and merger for this packet stream
      //adf::pktsplit<PACKET_CLUSTER_SIZE> splitter; // this may need to be an array outside loop/
      //adf::pktmerge<PACKET_CLUSTER_SIZE> merger;

      //splitter = adf::pktsplit<PACKET_CLUSTER_SIZE>::create();
      //merger   = adf::pktmerge<PACKET_CLUSTER_SIZE>::create();

      for(int j = 0; j < PACKET_CLUSTER_SIZE; j++)
      {
        int index = i*PACKET_CLUSTER_SIZE + j;

        //my_abc_kernel[index] = adf::kernel::create(abc_kernel);
        my_partials_kernel[index] = adf::kernel::create(partials_kernel);

        // Primary inputs to the AIE array
        x_in[index] = adf::input_plio::create("x_in_"+std::to_string(index), adf::plio_128_bits, "golden_data/partials/x_in"+std::to_string(index)+".dat");
        outplio_partials[index] = adf::output_plio::create("outplio_partials_"+std::to_string(index), adf::plio_128_bits, "simdata/partials"+std::to_string(index)+".dat");

        // Input connections
        adf::connect<adf::stream> net_in(x_in[index].out[0], my_partials_kernel[index].in[0]); // x-coords
        adf::fifo_depth(net_in) = FIFO_SIZE; // This FIFO allows the host to send data for many nets in bursts

        // Input connections for my_partials_kernel
        //adf::connect<adf::stream> net_xa(my_abc_kernel[index].out[0], my_partials_kernel[index].in[0]);
        //adf::connect<adf::stream> net_bc(my_abc_kernel[index].out[1], my_partials_kernel[index].in[1]);
        //adf::fifo_depth(net_xa) = FIFO_SIZE; // 140 is large enough to handle nets of size 8 without stalls
        //                                    //  or nets of size 9 (with minor stalls)
        //                                  //   add or subtract 24 to the buffer size to increase net size 
        //                                  //    nets larger than 8 should be handled by the host code
        //adf::fifo_depth(net_bc) = FIFO_SIZE; // this one doesn't need a fifo

        adf::connect<adf::stream> net_out(my_partials_kernel[index].out[0], outplio_partials[index].in[0]);
        adf::fifo_depth(net_out) = FIFO_SIZE; // this one doesn't need a fifo

        //adf::source(my_abc_kernel[index]) = "abc_kernel.cpp";
        //adf::runtime<adf::ratio>(my_abc_kernel[index]) = 0.5;

        adf::source(my_partials_kernel[index]) = "partials/partials_kernel.cpp";
        adf::runtime<adf::ratio>(my_partials_kernel[index]) = 0.9;

        #ifdef DEBUG_OUTPUT
            // Optional outputs for debugging intermediate terms
            //adf::output_plio outplio_xa[PARTIALS_GRAPH_COUNT], outplio_bc[PARTIALS_GRAPH_COUNT];
            //outplio_xa[i] = adf::output_plio::create("outplio_xa"+std::to_string(i), adf::plio_32_bits, "simdata/xa"+std::to_string(i)+".dat");
            //outplio_bc[i] = adf::output_plio::create("outplio_bc"+std::to_string(i), adf::plio_32_bits, "simdata/bc"+std::to_string(i)+".dat");
            
            // Connections for debugging terms
            //adf::connect<adf::stream>(my_abc_kernel[i].out[0], outplio_xa[i].in[0]);
            //adf::connect<adf::stream>(my_abc_kernel[i].out[1], outplio_bc[i].in[0]);
        #endif
      }
    }
  }
};
