#ifndef PL_ALGO_FIELD_SOLVE_PL_HPP
#define PL_ALGO_FIELD_SOLVE_PL_HPP

// field_solve_pl -- the ENTIRE electrostatic field solve on the PL, no AIE. Mirrors
// model/density_model.cpp forward_a_uv + spectral + inverse_fields (== sw_only
// Density.cpp compute_a_uv_DCT / compute_eField_DCT), built on fft_pl.hpp's PL DCT.
//
//   a_uv = DCT_x( DCT_y(rho) )                              (forward 2D DCT)
//   Ex_hat = a_uv * w_u / (w_u^2 + w_v^2),  Ey_hat = a_uv * w_v / (...)   ([0][0]=0)
//   Ex = IDXST_x( IDCT_y(Ex_hat) ),  Ey = IDCT_x( IDXST_y(Ey_hat) )       (inverse)
//
// Row-major NxN float grids in BRAM. N = FFT_N (= PL_GRID). This is the PL-only baseline
// (correctness first); the AIE 1024 datapath remains the throughput path.

#include "fft_pl.hpp"

namespace plalgo {

enum fs_xform { FS_DCT = 0, FS_IDCT = 1, FS_IDXST = 2 };

// Apply a 1D transform (selected by `xf`) to every row of a row-major NxN grid. HLS forbids
// indirect (function-pointer) calls, so the transform is selected with a direct branch.
static void fs_rows(const float in[FFT_N * FFT_N], float out[FFT_N * FFT_N], int xf) {
row:
    for (int r = 0; r < FFT_N; r++) {
        float ri[FFT_N], ro[FFT_N];
        for (int c = 0; c < FFT_N; c++) ri[c] = in[r * FFT_N + c];
        if      (xf == FS_DCT)  dct_1d_pl(ri, ro);
        else if (xf == FS_IDCT) idct_1d_pl(ri, ro);
        else                    idxst_1d_pl(ri, ro);
        for (int c = 0; c < FFT_N; c++) out[r * FFT_N + c] = ro[c];
    }
}

static void fs_transpose(const float in[FFT_N * FFT_N], float out[FFT_N * FFT_N]) {
tr_i:
    for (int i = 0; i < FFT_N; i++)
tr_j:
        for (int j = 0; j < FFT_N; j++) {
#pragma HLS PIPELINE II=1
            out[i * FFT_N + j] = in[j * FFT_N + i];
        }
}

// Full field solve: rho -> Ex, Ey. tmpA/tmpB are caller-provided NxN scratch.
static void field_solve_pl(const float rho[FFT_N * FFT_N],
                           float Ex[FFT_N * FFT_N], float Ey[FFT_N * FFT_N],
                           float tmpA[FFT_N * FFT_N], float tmpB[FFT_N * FFT_N]) {
    const int N = FFT_N;
    // ---- forward 2D DCT: a_uv = transpose(DCT_rows(transpose(DCT_rows(rho)))) ----
    fs_rows(rho, tmpA, FS_DCT);           // DCT along rows (y)
    fs_transpose(tmpA, tmpB);
    fs_rows(tmpB, tmpA, FS_DCT);          // DCT along rows (now x)
    fs_transpose(tmpA, tmpB);             // tmpB = a_uv

    // ---- spectral multiply -> Ex_hat (in Ex), Ey_hat (in Ey) ----
spec_u:
    for (int u = 0; u < N; u++)
spec_v:
        for (int v = 0; v < N; v++) {
#pragma HLS PIPELINE II=1
            const int idx = u * N + v;
            if (u == 0 && v == 0) { Ex[idx] = 0.0f; Ey[idx] = 0.0f; continue; }
            const float w_u = 2.0f * FFT_PI * u / N;
            const float w_v = 2.0f * FFT_PI * v / N;
            const float denom = w_u * w_u + w_v * w_v;
            const float a = tmpB[idx];
            Ex[idx] = a * w_u / denom;
            Ey[idx] = a * w_v / denom;
        }

    // ---- inverse: Ex = IDXST_x(IDCT_y(Ex_hat)); Ey = IDCT_x(IDXST_y(Ey_hat)) ----
    fs_rows(Ex, tmpA, FS_IDCT);   fs_transpose(tmpA, Ex);      // Ex: IDCT along y, transpose
    fs_rows(Ex, tmpA, FS_IDXST);  fs_transpose(tmpA, Ex);      //     IDXST along x, transpose back
    fs_rows(Ey, tmpA, FS_IDXST);  fs_transpose(tmpA, Ey);      // Ey: IDXST along y, transpose
    fs_rows(Ey, tmpA, FS_IDCT);   fs_transpose(tmpA, Ey);      //     IDCT along x, transpose back
}

} // namespace plalgo

#endif // PL_ALGO_FIELD_SOLVE_PL_HPP
