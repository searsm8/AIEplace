
#include "gmio_graph.h"
#include "system_settings.h"
#include "kernels.h"

namespace transform = xf::dsp::aie::fft;

/* IDXST subgraph
 * IDXST is defined as an IDCT with a shuffled input
 * and each term multiplied by (-1)^n
 */
class idxst_subgraph : public gmio_graph {
private:
  kernel IDXST_shuffle_kernel;
  kernel IDCTpre_kernel; //IDCT
  kernel unshuffle_kernel; //IDCT
  kernel IDXST_signs_kernel;
  
public:
  idxst_subgraph(){
		gmio_in = input_gmio::create("idxst_gmio_in", 64, 1000);
		gmio_out= output_gmio::create("idxst_gmio_out", 64, 1000);

    IDXST_shuffle_kernel = kernel::create(idxst_shuffle);
    IDCTpre_kernel   = kernel::create(idct_preprocess); //IDCT
    unshuffle_kernel = kernel::create(unshuffle); //IDCT
    IDXST_signs_kernel = kernel::create(idxst_signs);

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect< window<4*2*POINT_SIZE> > net_in      (gmio_in.out[0], IDXST_shuffle_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idct    (IDXST_shuffle_kernel.out[0], IDCTpre_kernel.in[0]); // IDCT
    connect< window<4*2*POINT_SIZE> > net_pre     (IDCTpre_kernel.out[0], FFT_subgraph.in[0]); //IDCT
    connect< window<4*2*POINT_SIZE> > net_fft     (FFT_subgraph.out[0], unshuffle_kernel.in[0]); //IDCT
    connect< window<4*2*POINT_SIZE> > net_dct_out (unshuffle_kernel.out[0], IDXST_signs_kernel.in[0]);
    connect< window<4*2*POINT_SIZE> > net_out     (IDXST_signs_kernel.out[0], gmio_out.in[0]);

    // Associate kernels with Source files and set runtime ratio
    source(IDXST_shuffle_kernel) = "kernels/idxst_shuffle.cpp";
    runtime<ratio>(IDXST_shuffle_kernel) = 0.5;

    source(IDCTpre_kernel) = "kernels/idct_preprocess.cpp";
    runtime<ratio>(IDCTpre_kernel) = 0.5;

    source(unshuffle_kernel) = "kernels/unshuffle.cpp";
    runtime<ratio>(unshuffle_kernel) = 0.5;

    source(IDXST_signs_kernel) = "kernels/idxst_signs.cpp";
    runtime<ratio>(IDXST_signs_kernel) = 0.5;
  }
};
