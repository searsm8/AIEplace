// top.cpp -- standalone PL hardware harness for pl_algo's FFT-based 1D transform module
// (vck5000/pl/src/pl_algo/src/modules/fft_pl.hpp: plalgo::dct_1d_pl / idct_1d_pl / idxst_1d_pl).
//
// Wraps the REAL module header directly (via -I, see ../../Makefile) -- this is not a
// port/copy, so any future edit to fft_pl.hpp is exercised here unchanged. Two DDR (m_axi)
// ports + a scalar mode select, same shape as ../../add1_pl/src/pl/top.cpp: NO AIE graph, NO
// AXIS stream ports, NO link.cfg connectivity. mode picks which of the three transforms runs:
//   0 = DCT, 1 = IDCT, 2 = IDXST  (see host_interface.hpp TFH_* / fft_pl.hpp for the math).
//
// N (the transform length) is PL_GRID, a compile-time constant baked into fft_pl.hpp's
// FFT_N/FFT_LOG -- see ../../Makefile for the -DPL_GRID default (64, matching
// vck5000/test/fft_pl_test.cpp's small-grid tier-1 golden check).

#ifndef PL_GRID
#define PL_GRID 64
#endif
#include "modules/fft_pl.hpp"

extern "C" {
void fft_pl_top(const float* in, float* out, int mode) {
#pragma HLS INTERFACE m_axi port=in  offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=out offset=slave bundle=gmem1
#pragma HLS INTERFACE s_axilite port=in     bundle=control
#pragma HLS INTERFACE s_axilite port=out    bundle=control
#pragma HLS INTERFACE s_axilite port=mode   bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    if (mode == 0)      plalgo::dct_1d_pl(in, out);
    else if (mode == 1) plalgo::idct_1d_pl(in, out);
    else                plalgo::idxst_1d_pl(in, out);
}
} // extern "C"
