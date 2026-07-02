#pragma once

#include <adf.h>
#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

#include "fft_ifft_dit_1ch_graph.hpp"
namespace dsplib = xf::dsp::aie;

#define FFT_POINTSIZE 1024
#define FFT_CASCADE_LEN 10
#define FFT_BITSHIFT 0
#define FFT_DYNAMIC_POINTSIZE 0

class FFTGraph : public adf::graph {
  public:
    adf::port<input> in;
    adf::port<output> out;

    FFTGraph()
    {
      dsplib::fft::dit_1ch::fft_ifft_dit_1ch_graph<
        cfloat,                // data type
        cfloat,                // twiddle type
        FFT_POINTSIZE,              // length of a row or col (should be num of rows/cols, assuming square matrix)
        1,                          // 1=FFT, 0=iFFT
        FFT_BITSHIFT,               // bitshift before output
        FFT_CASCADE_LEN,            // Pipeline cascade length (max log2(FFT_POINTSIZE)
        FFT_DYNAMIC_POINTSIZE       // 0=disable, 1=enable dynamic pointsize
        > fft_pipeline;

      runtime<ratio>(*fft_pipeline.getKernels()) = 1.0;

      adf::connect<adf::stream>(in, fft_pipeline.in[0]);
      adf::connect<adf::stream>(fft_pipeline.out[0], out);

    }
};

class DensityGraph : public adf::graph {
  public:
    adf::input_plio fft_in;
    adf::output_plio fft_out;

    FFTGraph fft;

    DensityGraph() {
      adf::location<FFTGraph>(fft) = area_group({
          {aie_tile, 0, 0, 10, 0},
          {shim_tile, 0, 0, 49, 0}
          });

      fft_in = adf::input_plio::create("fft_in", adf::plio_128_bits, "test_data/density/input.dat");
      fft_out = adf::output_plio::create("fft_out", adf::plio_128_bits, "test_data/density/golden.dat");

      adf::connect<adf::stream>(fft_in.out[0], fft.in);
      adf::connect<adf::stream>(fft.out, fft_out.in[0]);
    }

};
