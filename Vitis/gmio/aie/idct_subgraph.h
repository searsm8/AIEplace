
#include "gmio_graph.h"
#include "system_settings.h"
#include "kernels.h"

namespace transform = xf::dsp::aie::fft;

class idct_subgraph : public gmio_graph {
private:
  kernel IDCTpre_kernel;
  kernel unshuffle_kernel;

public:
  idct_subgraph(){
		gmio_in = input_gmio::create("idct_gmio_in", 64, 1000);
		gmio_out= output_gmio::create("idct_gmio_out", 64, 1000);

    IDCTpre_kernel   = kernel::create(idct_preprocess);
    unshuffle_kernel = kernel::create(unshuffle);

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect< window<4*2*POINT_SIZE> > net_in      (gmio_in.out[0], IDCTpre_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_pre     (IDCTpre_kernel.out[0], FFT_subgraph.in[0]);
    connect< window<4*2*POINT_SIZE> > net_fft     (FFT_subgraph.out[0], unshuffle_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_dct_out (unshuffle_kernel.out[0], gmio_out.in[0]);

    // Associate kernels with Source files and set runtime ratio
    source(IDCTpre_kernel)   = "kernels/idct_preprocess.cpp";
    source(unshuffle_kernel) = "kernels/unshuffle.cpp";

    runtime<ratio>(IDCTpre_kernel)   = 0.5;
    runtime<ratio>(unshuffle_kernel) = 0.5;
  }
};
