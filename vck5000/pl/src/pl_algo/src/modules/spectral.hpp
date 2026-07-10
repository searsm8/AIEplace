#ifndef PL_ALGO_SPECTRAL_HPP
#define PL_ALGO_SPECTRAL_HPP

// spectral -- Stage 4: the ePlace spectral multiply that turns the density spectrum a_uv
// into a field spectrum (Ex_hat or Ey_hat), the step between the forward 2D DCT and the
// inverse field passes. Reference: sw_only Density.cpp::compute_eField_DCT.
//
//   Ex_hat[u][v] = a_uv[u][v] * w_u / (w_u^2 + w_v^2)
//   Ey_hat[u][v] = a_uv[u][v] * w_v / (w_u^2 + w_v^2)
//   w_u = 2*pi*u/N,  w_v = 2*pi*v/N,   [0][0] = 0 (drops the DC term / avoids /0).
//
// One pass produces one field: `axis` picks the numerator (0 -> w_u for Ex, 1 -> w_v for
// Ey). Two passes over a_uv give both fields, reusing the single dct_in/dct_out plumbing
// (no extra gmem bundle). a_uv and the field are row-major u*N+v (u = row, matching the
// a_uv orientation the forward 2D DCT produces).
//
// v1 is a plain scalar elementwise pass (II=1, 1 float/beat) -- correctness first; widening
// the a_uv read / field write to 512-bit beats (static per-lane offsets, like dct_load_rows)
// is a later bandwidth optimization, not a correctness concern.

#include "../formats.hpp"

namespace plalgo {

static void spectral_multiply(const float* a_uv, float* field, int axis) {
    const int   N      = GRID;
    const float TWO_PI = 6.28318530717959f;

rows:
    for (int u = 0; u < N; u++) {
        const float w_u = TWO_PI * u / N;
    cols:
        for (int v = 0; v < N; v++) {
#pragma HLS PIPELINE II=1
            const float w_v   = TWO_PI * v / N;
            const float denom = w_u * w_u + w_v * w_v;
            const float coeff = a_uv[u * N + v];
            float out;
            if (u == 0 && v == 0) out = 0.0f;                       // drop DC
            else out = (axis == 0) ? (coeff * w_u / denom)          // Ex_hat
                                   : (coeff * w_v / denom);         // Ey_hat
            field[u * N + v] = out;
        }
    }
}

} // namespace plalgo

#endif // PL_ALGO_SPECTRAL_HPP
