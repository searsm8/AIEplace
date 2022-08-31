
#include "system_settings.h"
#include "kernels.h"


namespace transform = xf::dsp::aie::fft;

class dct_subsystem : public adf::graph {
private:
  // Subgraph names are prefixed with "s_"
  transform::dit_1ch::fft_ifft_dit_1ch_graph<FFT_DATA_TYPE, TWIDDLE_TYPE, POINT_SIZE, FFT_DIR, TP_SHIFT, TP_CASC_LEN, TP_DYN_PT_SIZE, TP_WINDOW_VSIZE> s_fft1;
  transform::dit_1ch::fft_ifft_dit_1ch_graph<FFT_DATA_TYPE, TWIDDLE_TYPE, POINT_SIZE, FFT_DIR, TP_SHIFT, TP_CASC_LEN, TP_DYN_PT_SIZE, TP_WINDOW_VSIZE> s_fft2;

  // Kernel names are prefixed with "k_"
  kernel k_Shuf;
  kernel k_DCT_post;
  kernel k_IDCT_pre;
  kernel k_Unshuf;
  //kernel kTrans1;
public:
  input_plio  in; 
  output_plio dct_out;
  output_plio idct_out;
  output_plio idct_pre;
  output_plio idct_fft;
  output_plio transpose_out;
  //input_plio shuf_in;
  //output_plio shuf_out;
  //output_plio unshuf_out;

  dct_subsystem(){

    in  = input_plio::create("In", plio_32_bits, "data/input.dat");
    dct_out = output_plio::create("DCT_Out", plio_32_bits, "data/dct_output.dat");
    idct_out = output_plio::create("IDCT_Out", plio_32_bits, "data/idct_output.dat");
    idct_pre = output_plio::create("IDCT_pre", plio_32_bits, "data/idct_pre.dat");
    idct_fft = output_plio::create("IDCT_fft", plio_32_bits, "data/idct_fft.dat");
    //transpose_out = output_plio::create("trans", plio_32_bits, "data/transpose_out.dat");
    //shuf_in  = input_plio::create("shufIn", plio_32_bits, "data/shuffle.dat");
    //shuf_out = output_plio::create("shufOut", plio_32_bits, "data/shuffle_output.dat");
    //unshuf_out = output_plio::create("unshufOut", plio_32_bits, "data/unshuffle_output.dat");

    k_Shuf     = kernel::create(shuffle);
    k_DCT_post = kernel::create(dct_postprocess);
    k_IDCT_pre = kernel::create(idct_preprocess);
    k_Unshuf   = kernel::create(unshuffle);
    //kTrans1 = kernel::create(transpose);

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    // TODO: Possibly reduce communication overhead by using floats for real-only values
    connect< window<4*2*POINT_SIZE> > net_in      (in.out[0], k_Shuf.in[0]);
    connect< window<4*2*POINT_SIZE> > net_dct0    (k_Shuf.out[0], s_fft1.in[0]);
    connect< window<4*2*POINT_SIZE> > net_dct1    (s_fft1.out[0], k_DCT_post.in[0]);
    connect< window<4*2*POINT_SIZE> > net_dct_out (k_DCT_post.out[0], dct_out.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idct_in (k_DCT_post.out[0], k_IDCT_pre.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idct1   (k_IDCT_pre.out[0], s_fft2.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idct2   (s_fft2.out[0], k_Unshuf.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idct_out(k_Unshuf.out[0], idct_out.in[0]);

    connect< window<4*2*POINT_SIZE> > net_idctpre (k_IDCT_pre.out[0], idct_pre.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idctfft (s_fft2.out[0], idct_fft.in[0]);



    source(k_Shuf)     = "kernels/shuffle.cpp";
    source(k_DCT_post) = "kernels/dct_postprocess.cpp";
    source(k_IDCT_pre) = "kernels/idct_preprocess.cpp";
    source(k_Unshuf)   = "kernels/unshuffle.cpp";
    //source(k_Trans1) = "kernels/transpose.cpp";

    runtime<ratio>(k_Shuf)     = 0.5;
    runtime<ratio>(k_DCT_post) = 0.5;
    runtime<ratio>(k_IDCT_pre) = 0.5;
    runtime<ratio>(k_Unshuf)   = 0.5;
    //runtime<ratio>(k_Trans1) = 0.5;
  }
};
