// DensityFFTGraph.h
// pl_algo density-field FFT pool. The AIE does ONLY the forward complex FFT; all
// DCT/IDCT/IDXST pre/post-processing (shuffle, twiddle, Re, sign-flip, DC-scale)
// runs on the PL. ONE forward-FFT config serves all three transforms -- confirmed
// by the Stage 0 math model (model/density_model.cpp) and by markv1's three graphs
// all using FFT_DIR=1. Stage 2 brings up a SINGLE lane; Stage 3 scales to FFT_LANES.
//
// Wiring: input_plio (128-bit, 2 cfloat/beat) -> fft_ifft_dit_1ch_graph -> output_plio.
// The PLIO ports are driven by the PL `top` kernel's fft_to_aie/fft_from_aie streams
// (link.cfg stream_connect); the .dat paths are only used by standalone x86/aiesim.
#pragma once

#include "system_settings.h"
#include "fft_ifft_dit_1ch_graph.hpp"
#include <string>

namespace dsplib_fft = xf::dsp::aie::fft;

#ifndef DENSITY_FFT_INSTANCES
#define DENSITY_FFT_INSTANCES 1
#endif

// FFT configuration (one forward FFT, serves DCT/IDCT/IDXST via PL pre/post).
#define FFT_POINT_SIZE   1024
#define FFT_NIFFT        1     // 1 = forward FFT
#define FFT_SHIFT        0     // no output scaling (Makhoul identity needs the raw sum)
#define FFT_CASC_LEN     1     // throughput knob; correctness-neutral
#define FFT_DYN_PT_SIZE  0
#define FFT_WINDOW_VSIZE FFT_POINT_SIZE
// TP_API=0 (window/iobuffer): a single PLIO drives the FFT's input buffer (the
// aiecompiler bridges the PLIO stream to the iobuffer). TP_API=1 (stream) splits
// across the AIE's two physical stream ports (in[0]+in[1]) -- a single PLIO per
// port can't satisfy that, so window API matches the one-PLIO-per-lane wiring
// (same default markv1's graphs used).
#define FFT_API          0

class DensityFFTGraph : public adf::graph {
private:
  dsplib_fft::dit_1ch::fft_ifft_dit_1ch_graph<
      cfloat,            // TT_DATA
      cfloat,            // TT_TWIDDLE
      FFT_POINT_SIZE,
      FFT_NIFFT,
      FFT_SHIFT,
      FFT_CASC_LEN,
      FFT_DYN_PT_SIZE,
      FFT_WINDOW_VSIZE,
      FFT_API> fft[DENSITY_FFT_INSTANCES];

public:
  adf::input_plio  fft_in[DENSITY_FFT_INSTANCES];
  adf::output_plio fft_out[DENSITY_FFT_INSTANCES];

  DensityFFTGraph() {
    for (int i = 0; i < DENSITY_FFT_INSTANCES; i++) {
      fft_in[i] = adf::input_plio::create(
          "fft_in_" + std::to_string(i), adf::plio_128_bits,
          "golden_data/density_fft/fft_in_" + std::to_string(i) + ".dat");
      fft_out[i] = adf::output_plio::create(
          "fft_out_" + std::to_string(i), adf::plio_128_bits,
          "simdata/density_fft/fft_out_" + std::to_string(i) + ".dat");

      adf::connect<adf::stream>(fft_in[i].out[0],  fft[i].in[0]);
      adf::connect<adf::stream>(fft[i].out[0],     fft_out[i].in[0]);
    }
  }
};
