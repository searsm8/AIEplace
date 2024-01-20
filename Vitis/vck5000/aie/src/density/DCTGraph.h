#pragma once

#include "density_kernels.h"
#include "fft_ifft_dit_1ch_graph.hpp"
namespace transform = xf::dsp::aie::fft;

// Parameters for fft graph
#define FFT_DIR 1 // 0 for ifft, 1 for fft
#define TP_SHIFT 0 // Bit shift
#define TP_CASC_LEN 1
#define TP_DYN_PT_SIZE 0
#define TP_WINDOW_VSIZE POINT_SIZE

class DCTGraph : public adf::graph {
private:
  transform::dit_1ch::fft_ifft_dit_1ch_graph<
    FFT_DATA_TYPE,
    TWIDDLE_TYPE,
    POINT_SIZE,
    FFT_DIR,
    TP_SHIFT,
    TP_CASC_LEN,
    TP_DYN_PT_SIZE,
    TP_WINDOW_VSIZE> FFT_subgraph;

  adf::kernel DCT_shuffle_kernel;
  adf::kernel DCT_post_kernel;
  adf::input_plio DCT_in;
  adf::output_plio DCT_out;

public:
  DCTGraph(){
		//gmio_in = input_gmio::create("dct_gmio_in", 64, 1000);
		//gmio_out= output_gmio::create("dct_gmio_out", 64, 1000);
    DCT_in  = adf::input_plio::create("DCT_in", adf::plio_32_bits, "golden_data/density/input.dat");
    DCT_out = adf::output_plio::create("DCT_out", adf::plio_32_bits, "simdata/dct_output.dat");
    
    DCT_shuffle_kernel = kernel::create(dct_shuffle);
    DCT_post_kernel = kernel::create(dct_postprocess);

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect< window<4*2*POINT_SIZE> > net_in      (DCT_in.out[0], DCT_shuffle_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_shuffle (DCT_shuffle_kernel.out[0], FFT_subgraph.in[0]);
    connect< window<4*2*POINT_SIZE> > net_fft     (FFT_subgraph.out[0], DCT_post_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_dct_out (DCT_post_kernel.out[0], DCT_out.in[0]);

    // Associate kernels with Source files and set runtime ratio
    source(DCT_shuffle_kernel) = "dct_shuffle.cpp";
    source(DCT_post_kernel) = "dct_postprocess.cpp";

    runtime<ratio>(DCT_shuffle_kernel) = 1;
    runtime<ratio>(DCT_post_kernel) = 1;
  }
};
