#pragma once

#include "density_kernels.h"
#include "fft_ifft_dit_1ch_graph.hpp"
namespace transform = xf::dsp::aie::fft;

// Parameters for fft graph
#define FFT_DIR 1 // 0 for ifft, 1 for fft
#define TP_SHIFT 0 // Bit shift
#define TP_CASC_LEN 1 // increase this to improve throughput using more kernels (max casc_len = log2(POINT_SIZE))
#define TP_DYN_PT_SIZE 0
#define TP_WINDOW_VSIZE POINT_SIZE
#define API_IO 1 // 0 for window, 1 for streams (must use streams if using PARALLEL_POWER)
#define PARALLEL_POWER 0 // trade-off to use more cores for improved performance

class IDCTGraph : public adf::graph {
private:
  transform::dit_1ch::fft_ifft_dit_1ch_graph<
    FFT_DATA_TYPE,
    TWIDDLE_TYPE,
    POINT_SIZE,
    FFT_DIR,
    TP_SHIFT,
    TP_CASC_LEN,
    TP_DYN_PT_SIZE,
    TP_WINDOW_VSIZE
    //,
    //API_IO,
    //PARALLEL_POWER 
    > FFT_subgraph;

  adf::input_plio IDCT_in;
  adf::output_plio IDCT_out;

  adf::kernel IDCT_pre_kernel;
  adf::kernel IDCT_unshuffle_kernel;

public:
  IDCTGraph(){
    IDCT_in  = adf::input_plio::create("IDCT_in", adf::plio_32_bits, "golden_data/density/dct_2d_output.dat");
    IDCT_out = adf::output_plio::create("IDCT_out", adf::plio_32_bits, "simdata/idct_output.dat");
    
    IDCT_pre_kernel = kernel::create(idct_preprocess);
    IDCT_unshuffle_kernel= kernel::create(idct_unshuffle);

    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect<adf::stream> net_in      (IDCT_in.out[0], IDCT_pre_kernel.in[0]);
    connect<adf::stream> net_pre     (IDCT_pre_kernel.out[0], FFT_subgraph.in[0]);
    connect<adf::stream> net_fft     (FFT_subgraph.out[0], IDCT_unshuffle_kernel.in[0]);
    connect<adf::stream> net_idct_out(IDCT_unshuffle_kernel.out[0], IDCT_out.in[0]);

    // Associate kernels with Source files and set runtime ratio
    source(IDCT_pre_kernel) = "idct_preprocess.cpp";
    source(IDCT_unshuffle_kernel) = "idct_unshuffle.cpp";

    runtime<adf::ratio>(IDCT_pre_kernel) = 1;
    runtime<adf::ratio>(IDCT_unshuffle_kernel) = 1;
  }
};
