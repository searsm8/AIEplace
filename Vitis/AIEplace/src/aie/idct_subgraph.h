
#include "system_settings.h"
#include "kernels.h"

namespace transform = xf::dsp::aie::fft;

class idct_subgraph : public adf::graph {
private:
  transform::dit_1ch::fft_ifft_dit_1ch_graph<FFT_DATA_TYPE, TWIDDLE_TYPE, POINT_SIZE, FFT_DIR, TP_SHIFT, TP_CASC_LEN, TP_DYN_PT_SIZE, TP_WINDOW_VSIZE> FFT_subgraph;

  kernel IDCTpre_kernel;
  kernel unshuffle_kernel;
public:
  port<input> in; 
  port<output> out;

  idct_subgraph(){
    IDCTpre_kernel   = kernel::create(idct_preprocess);
    unshuffle_kernel = kernel::create(unshuffle);

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect< window<4*2*POINT_SIZE> > net_in      (in, IDCTpre_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_pre     (IDCTpre_kernel.out[0], FFT_subgraph.in[0]);
    connect< window<4*2*POINT_SIZE> > net_fft     (FFT_subgraph.out[0], unshuffle_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_dct_out (unshuffle_kernel.out[0], out);

    // Associate kernels with Source files and set runtime ratio
    source(IDCTpre_kernel)   = "kernels/idct_preprocess.cpp";
    source(unshuffle_kernel) = "kernels/unshuffle.cpp";

    runtime<ratio>(IDCTpre_kernel)   = 0.5;
    runtime<ratio>(unshuffle_kernel) = 0.5;
  }
};
