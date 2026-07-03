#pragma once

#include <adf.h>
#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

#include "fft_ifft_dit_1ch_graph.hpp"
namespace dsplib = xf::dsp::aie;

#define DENSITY_CHANNELS 8
#define FFT_POINTSIZE 1024
#define FFT_CASCADE_LEN 10
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
        {aie_tile,  start_col,                   row,
                    start_col + (FFT_CASCADE_LEN/2), row+1 },
        {shim_tile, 0, 0, 49, 0}
      });
    }
};

class IFFTGraph : public adf::graph {
  public:
    adf::port<input> in;
    adf::port<output> out;

    IFFTGraph() {
      dsplib::fft::dit_1ch::fft_ifft_dit_1ch_graph<
        cfloat,                // data type
        cfloat,                // twiddle type
        FFT_POINTSIZE,              // length of a row or col (should be num of rows/cols, assuming square matrix)
        0,                          // 1=FFT, 0=iFFT
        FFT_BITSHIFT,               // bitshift before output
        FFT_CASCADE_LEN,            // Pipeline cascade length (max log2(FFT_POINTSIZE)
        FFT_DYNAMIC_POINTSIZE       // 0=disable, 1=enable dynamic pointsize
        > ifft_pipeline;

      adf::kernel* fft_kernels = ifft_pipeline.getKernels();
      for (int i = 0; i < FFT_CASCADE_LEN; i++) {
      	runtime<ratio>(*(fft_kernels+i)) = 1.0;
      }

      adf::connect<adf::stream>(in, ifft_pipeline.in[0]);
      adf::connect<adf::stream>(ifft_pipeline.out[0], out);

    }

    void place(int start_col, int row) {
      adf::location<adf::graph>(*this) = area_group({
        {aie_tile,  start_col,                   row,
                    start_col + (FFT_CASCADE_LEN/2), row+1 },
        {shim_tile, 0, 0, 49, 0}
      });
    }
};

class DensityGraph : public adf::graph {
  public:
    adf::input_plio fft_in[DENSITY_CHANNELS];
    adf::output_plio fft_out[DENSITY_CHANNELS];

    adf::input_plio ifft_psi_in[DENSITY_CHANNELS];
    adf::output_plio ifft_psi_out[DENSITY_CHANNELS];

    adf::input_plio ifft_ksi_in[DENSITY_CHANNELS];
    adf::output_plio ifft_ksi_out[DENSITY_CHANNELS];

    adf::input_plio idxst_ksi_in[DENSITY_CHANNELS];
    adf::output_plio idxst_ksi_out[DENSITY_CHANNELS];

    FFTGraph fft[DENSITY_CHANNELS];
    IFFTGraph ifft_psi[DENSITY_CHANNELS];
    IFFTGraph ifft_ksi[DENSITY_CHANNELS];
    IFFTGraph idxst_ksi[DENSITY_CHANNELS];

    DensityGraph() {

      for (int i = 0; i < DENSITY_CHANNELS; i++) {

        fft[i].place((i%2)*((FFT_CASCADE_LEN/2)+1), (i & ~1));

        fft_in[i] = adf::input_plio::create("fft_in"+std::to_string(i), adf::plio_128_bits, "test_data/density/input.dat");
        fft_out[i] = adf::output_plio::create("fft_out"+std::to_string(i), adf::plio_128_bits, "test_data/density/golden.dat");

        adf::connect<adf::stream>(fft_in[i].out[0], fft[i].in);
        adf::connect<adf::stream>(fft[i].out, fft_out[i].in[0]);

        ifft_psi[i].place((i%2)*((FFT_CASCADE_LEN/2)+1)+12, (i & ~1));

        ifft_psi_in[i] = adf::input_plio::create("ifft_psi_in"+std::to_string(i), adf::plio_128_bits, "test_data/density/input.dat");
        ifft_psi_out[i] = adf::output_plio::create("ifft_psi_out"+std::to_string(i), adf::plio_128_bits, "test_data/density/golden.dat");

        adf::connect<adf::stream>(ifft_psi_in[i].out[0], ifft_psi[i].in);
        adf::connect<adf::stream>(ifft_psi[i].out, ifft_psi_out[i].in[0]);

        ifft_ksi[i].place((i%2)*((FFT_CASCADE_LEN/2)+1)+24, (i & ~1));

        ifft_ksi_in[i] = adf::input_plio::create("ifft_ksi_in"+std::to_string(i), adf::plio_128_bits, "test_data/density/input.dat");
        ifft_ksi_out[i] = adf::output_plio::create("ifft_ksi_out"+std::to_string(i), adf::plio_128_bits, "test_data/density/golden.dat");

        adf::connect<adf::stream>(ifft_ksi_in[i].out[0], ifft_ksi[i].in);
        adf::connect<adf::stream>(ifft_ksi[i].out, ifft_ksi_out[i].in[0]);

        idxst_ksi[i].place((i%2)*((FFT_CASCADE_LEN/2)+1)+36, (i & ~1));

        idxst_ksi_in[i] = adf::input_plio::create("idxst_ksi_in"+std::to_string(i), adf::plio_128_bits, "test_data/density/input.dat");
        idxst_ksi_out[i] = adf::output_plio::create("idxst_ksi_out"+std::to_string(i), adf::plio_128_bits, "test_data/density/golden.dat");

        adf::connect<adf::stream>(idxst_ksi_in[i].out[0], idxst_ksi[i].in);
        adf::connect<adf::stream>(idxst_ksi[i].out, idxst_ksi_out[i].in[0]);

      }
    }

};

