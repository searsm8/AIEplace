
#include "system_settings.h"
#include "kernels.h"

namespace transform = xf::dsp::aie::fft;

class idxst_subgraph : public adf::graph {
private:
  idct_subgraph IDCT_subgraph;

  kernel IDXST_shuffle_kernel;
  kernel IDXST_signs_kernel;
  
public:
  port<input> in; 
  port<output> out;

  idxst_subgraph(){
    IDXST_shuffle_kernel = kernel::create(idxst_shuffle);
    IDXST_signs_kernel = kernel::create(idxst_signs);

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect< window<4*2*POINT_SIZE> > net_in      (in, IDXST_shuffle_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idct    (IDXST_shuffle_kernel.out[0], IDCT_subgraph.in);
    connect< window<4*2*POINT_SIZE> > net_signs   (IDCT_subgraph.out, IDXST_signs_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_out     (IDXST_signs_kernel.out[0], out);

    // Associate kernels with Source files and set runtime ratio
    source(IDXST_shuffle_kernel)  = "kernels/idxst_shuffle.cpp";
    source(IDXST_signs_kernel)  = "kernels/idxst_signs.cpp";

    runtime<ratio>(IDXST_shuffle_kernel)   = 0.5;
    runtime<ratio>(IDXST_signs_kernel)   = 0.5;
  }
};
