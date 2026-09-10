// top.cpp -- standalone PL hardware harness for pl_algo's FULL electrostatic field solve
// (vck5000/pl/src/pl_algo/src/modules/field_solve_pl.hpp: plalgo::field_solve_pl), the entire
// density solve -- forward 2D DCT -> spectral multiply -> inverse IDCT/IDXST -- on the PL with
// NO AIE. Built on the fft_pl.hpp DCT/IDCT/IDXST transforms already verified on hardware
// (../../fft_pl). The module is included directly (-I, see ../../Makefile), not copied.
//
// Three DDR (m_axi) ports (rho in, Ex out, Ey out), no scalars, no AIE graph, no AXIS stream
// ports, no link.cfg connectivity -- same framework as ../../fft_pl and ../../add1_pl. The
// N x N grids live in on-chip BRAM scratch (static below); this mirrors top.cpp's
// MODE_FIELD_SOLVE_PL exactly (copy DDR->BRAM, solve, copy BRAM->DDR).
//
// N (grid side) is PL_GRID, compile-time (baked into field_solve_pl.hpp's FFT_N via fft_pl.hpp).
// See ../../Makefile for the -DPL_GRID default (64, matching field_solve_test.cpp's small-grid
// golden). On-chip footprint = 5 * N*N floats (rho_,Ex_,Ey_,tA_,tB_) = 80 KB at N=64.

#ifndef PL_GRID
#define PL_GRID 64
#endif
#include "modules/field_solve_pl.hpp"

using namespace plalgo;

extern "C" {
void field_solve_top(const float* rho, float* Ex, float* Ey) {
#pragma HLS INTERFACE m_axi port=rho offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=Ex  offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=Ey  offset=slave bundle=gmem2
#pragma HLS INTERFACE s_axilite port=rho    bundle=control
#pragma HLS INTERFACE s_axilite port=Ex     bundle=control
#pragma HLS INTERFACE s_axilite port=Ey     bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    // On-chip BRAM scratch for the whole solve (same as MODE_FIELD_SOLVE_PL in pl_algo/top.cpp).
    static float rho_[(FFT_N * FFT_N)], Ex_[(FFT_N * FFT_N)], Ey_[(FFT_N * FFT_N)];
    static float tA_[(FFT_N * FFT_N)], tB_[(FFT_N * FFT_N)];

load:
    for (int i = 0; i < (FFT_N * FFT_N); i++) {
#pragma HLS PIPELINE II=1
        rho_[i] = rho[i];
    }
    field_solve_pl(rho_, Ex_, Ey_, tA_, tB_);
store:
    for (int i = 0; i < (FFT_N * FFT_N); i++) {
#pragma HLS PIPELINE II=1
        Ex[i] = Ex_[i];
        Ey[i] = Ey_[i];
    }
}
} // extern "C"
