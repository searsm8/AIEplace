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
#define API_IO 0 // 0 for window, 1 for streams (must use streams if using PARALLEL_POWER)
#define PARALLEL_POWER 0 // trade-off to use more cores for improved performance

class IDXSTGraph : public adf::graph {
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

  adf::input_plio IDXST_in;
  adf::output_plio IDXST_out;

  adf::kernel IDXST_shuffle_kernel;
  adf::kernel IDCT_pre_kernel;
  adf::kernel IDCT_unshuffle_kernel;
  adf::kernel IDXST_signs_kernel;

public:
  IDXSTGraph(){
    IDXST_in  = adf::input_plio::create("IDXST_in", adf::plio_32_bits, "golden_data/density/dct_2d_output.dat");
    IDXST_out = adf::output_plio::create("IDXST_out", adf::plio_32_bits, "simdata/idxst_output.dat");
    
    IDXST_shuffle_kernel = kernel::create(idxst_shuffle);
    IDCT_pre_kernel = kernel::create(idct_preprocess);
    IDCT_unshuffle_kernel= kernel::create(idct_unshuffle);
    IDXST_signs_kernel = kernel::create(idxst_signs);

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect< window<4*2*POINT_SIZE> > net_in      (IDXST_in.out[0], IDXST_shuffle_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_shuf    (IDXST_shuffle_kernel.out[0], IDCT_pre_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_pre     (IDCT_pre_kernel.out[0], FFT_subgraph.in[0]);
    connect< window<4*2*POINT_SIZE> > net_fft     (FFT_subgraph.out[0], IDCT_unshuffle_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idct_out(IDCT_unshuffle_kernel.out[0], IDXST_signs_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idxst_out(IDXST_signs_kernel.out[0], IDXST_out.in[0]);

    // Associate kernels with Source files and set runtime ratio
    source(IDXST_shuffle_kernel) = "idxst_shuffle.cpp";
    source(IDCT_pre_kernel) = "idct_preprocess.cpp";
    source(IDCT_unshuffle_kernel) = "idct_unshuffle.cpp";
    source(IDXST_signs_kernel) = "idxst_signs.cpp";

    runtime<ratio>(IDXST_shuffle_kernel) = 0.5;
    runtime<ratio>(IDCT_pre_kernel) = 0.5;
    runtime<ratio>(IDCT_unshuffle_kernel) = 0.5;
    runtime<ratio>(IDXST_signs_kernel) = 0.5;
  }
};
