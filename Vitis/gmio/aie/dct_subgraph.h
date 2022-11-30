
#include "gmio_graph.h"
#include "system_settings.h"
#include "kernels.h"

namespace transform = xf::dsp::aie::fft;

class dct_subgraph : public gmio_graph {
private:
  kernel shuffle_kernel;
  kernel DCTpost_kernel;

public:
  dct_subgraph(){
		gmio_in = input_gmio::create("dct_gmio_in", 64, 1000);
		gmio_out= output_gmio::create("dct_gmio_out", 64, 1000);
    
    shuffle_kernel = kernel::create(shuffle);
    DCTpost_kernel = kernel::create(dct_postprocess);

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect< window<4*2*POINT_SIZE> > net_in      (gmio_in.out[0], shuffle_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_shuf    (shuffle_kernel.out[0], FFT_subgraph.in[0]);
    connect< window<4*2*POINT_SIZE> > net_fft     (FFT_subgraph.out[0], DCTpost_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_dct_out (DCTpost_kernel.out[0], gmio_out.in[0]);

    // Associate kernels with Source files and set runtime ratio
    source(shuffle_kernel) = "shuffle.cpp";
    source(DCTpost_kernel) = "dct_postprocess.cpp";

    runtime<ratio>(shuffle_kernel) = 0.5;
    runtime<ratio>(DCTpost_kernel) = 0.5;
  }
};
