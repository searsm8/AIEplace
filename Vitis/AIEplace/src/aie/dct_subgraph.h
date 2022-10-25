
#include "system_settings.h"
#include "kernels.h"

namespace transform = xf::dsp::aie::fft;

class dct_subgraph : public adf::graph {
private:
  transform::dit_1ch::fft_ifft_dit_1ch_graph<FFT_DATA_TYPE, TWIDDLE_TYPE, POINT_SIZE, FFT_DIR, TP_SHIFT, TP_CASC_LEN, TP_DYN_PT_SIZE, TP_WINDOW_VSIZE> FFT_subgraph;

  kernel shuffle_kernel;
  kernel DCTpost_kernel;
public:
  port<input> in; 
  port<output> out;

  dct_subgraph(){
    shuffle_kernel = kernel::create(shuffle);
    DCTpost_kernel = kernel::create(dct_postprocess);

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect< window<4*2*POINT_SIZE> > net_in      (in, shuffle_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_shuf    (shuffle_kernel.out[0], FFT_subgraph.in[0]);
    connect< window<4*2*POINT_SIZE> > net_fft     (FFT_subgraph.out[0], DCTpost_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_dct_out (DCTpost_kernel.out[0], out);

    // Associate kernels with Source files and set runtime ratio
    source(shuffle_kernel) = "kernels/shuffle.cpp";
    source(DCTpost_kernel) = "kernels/dct_postprocess.cpp";

    runtime<ratio>(shuffle_kernel) = 0.5;
    runtime<ratio>(DCTpost_kernel) = 0.5;
  }
};
