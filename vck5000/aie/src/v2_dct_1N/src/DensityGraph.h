#pragma once

#include <adf.h>
#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

#include "fft_ifft_dit_1ch_graph.hpp"
namespace dsplib = xf::dsp::aie;

#ifndef DENSITY_CHANNELS
#define DENSITY_CHANNELS 8
#endif

#define FFT_POINTSIZE 1024
#define FFT_CASCADE_LEN 3
#define FFT_BITSHIFT 0
#define FFT_DYNAMIC_POINTSIZE 0

class FFTGraph : public adf::graph {
  public:
    adf::port<input> in;
    adf::port<output> out;

    FFTGraph() {
      dsplib::fft::dit_1ch::fft_ifft_dit_1ch_graph<
        cfloat,                // data type
        cfloat,                // twiddle type
        FFT_POINTSIZE,              // length of a row or col (should be num of rows/cols, assuming square matrix)
        1,                          // 1=FFT, 0=iFFT
        FFT_BITSHIFT,               // bitshift before output
        FFT_CASCADE_LEN,            // Pipeline cascade length (max log2(FFT_POINTSIZE)
        FFT_DYNAMIC_POINTSIZE       // 0=disable, 1=enable dynamic pointsize
        > fft_pipeline;

      adf::kernel* fft_kernels = fft_pipeline.getKernels();
      for (int i = 0; i < FFT_CASCADE_LEN; i++) {
      	runtime<ratio>(*(fft_kernels+i)) = 1.0;
      }

      adf::connect<adf::stream>(in, fft_pipeline.in[0]);
      adf::connect<adf::stream>(fft_pipeline.out[0], out);

    }

    void place(int start_col, int row) {
      adf::location<adf::graph>(*this) = area_group({
        {aie_tile,  start_col,   row,
                    start_col+1, row+1},
        {shim_tile, 0, 0, 49, 0}
      });
    }
};

class DensityGraph : public adf::graph {
  public:
    adf::input_plio fft_in[DENSITY_CHANNELS];
    adf::output_plio fft_out[DENSITY_CHANNELS];

    FFTGraph fft[DENSITY_CHANNELS];

    DensityGraph() {

      for (int i = 0; i < DENSITY_CHANNELS; i++) {

        fft[i].place(int(i/4) * 2, (i%4) * 2);

        fft_in[i] = adf::input_plio::create("fft_in"+std::to_string(i), adf::plio_64_bits, "test_data/density/input_64bit.dat");
        fft_out[i] = adf::output_plio::create("fft_out"+std::to_string(i), adf::plio_64_bits, "test_data/density/output_"+std::to_string(i)+".dat");

        adf::connect<adf::stream>(fft_in[i].out[0], fft[i].in);
        adf::connect<adf::stream>(fft[i].out, fft_out[i].in[0]);

      }
    }

};

