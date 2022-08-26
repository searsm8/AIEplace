
#include "system_settings.h"
#include "kernels.h"


namespace transform = xf::dsp::aie::fft;

class dct_subsystem : public adf::graph {
private:
  transform::dit_1ch::fft_ifft_dit_1ch_graph<FFT_DATA_TYPE, TWIDDLE_TYPE, POINT_SIZE, FFT_DIR, TP_SHIFT, TP_CASC_LEN, TP_DYN_PT_SIZE, TP_WINDOW_VSIZE> fft_graph;
  kernel kShuf;
  kernel kFFT2DCT1;
public:
  input_plio  in; 
  output_plio dct_out;
  //output_plio fft_out;
  //input_plio shuf_in;
  //output_plio shuf_out;

  dct_subsystem(){
    
    in  = input_plio::create("In", plio_32_bits, "data/dct_input.dat");
    dct_out = output_plio::create("Out", plio_32_bits, "data/dct_output.dat");
    //fft_out = output_plio::create("FFTOut", plio_32_bits, "data/fft_output.dat");
    //shuf_in  = input_plio::create("shufIn", plio_32_bits, "data/shuf_input.dat");
    //shuf_out = output_plio::create("shufOut", plio_32_bits, "data/shuf_output.dat");

    kFFT2DCT1 = kernel::create(fft_to_dct);
    kShuf = kernel::create(shuffle);

    connect< window<4*2*POINT_SIZE> > net_in (in.out[0], kShuf.in[0]);
    connect< window<4*2*POINT_SIZE> > net0 (kShuf.out[0], fft_graph.in[0]);
    connect< window<4*2*POINT_SIZE> > net1 (fft_graph.out[0], kFFT2DCT1.in[0]);
    //connect< window<4*2*POINT_SIZE> > net_fft_out(fft_graph.out[0], fft_out.in[0]);
    connect< window<4*1*POINT_SIZE> > net2 (kFFT2DCT1.out[0], dct_out.in[0]);


    source(kFFT2DCT1) = "kernels/fft_to_dct.cpp";
    source(kShuf) = "kernels/shuffle.cpp";

    runtime<ratio>(kFFT2DCT1) = 0.5;
    runtime<ratio>(kShuf) = 0.5;
  }
};
