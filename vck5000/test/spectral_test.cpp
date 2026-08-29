// Verify spectral.hpp (spectral_multiply) -- Stage 4 ePlace spectral multiply, the step between the
// forward 2D DCT and the inverse field passes. Turns the density spectrum a_uv into a field
// spectrum: Ex_hat = a_uv * w_u/(w_u^2+w_v^2), Ey_hat = a_uv * w_v/(w_u^2+w_v^2), with [0][0]=0.
// Reference: sw_only Density.cpp::compute_eField_DCT (lines 164-169), which computes exactly this
// per (u,v) with w_u=2*pi*u/N, w_v=2*pi*v/N and drops the DC term.
//
// THREE ASSERTIONS:
//   [1] EX (axis 0) -- module vs a double re-derivation of a_uv*w_u/denom. rel_rms; only the
//                      module's float w_u/denom rounding remains.
//   [2] EY (axis 1) -- same for a_uv*w_v/denom. Both axes share plumbing (one gmem bundle), so
//                      both must be exercised.
//   [3] DC DROP     -- field[0] (u=v=0, denom=0) must be EXACTLY 0 on both axes -- the guard that
//                      keeps a 0/0 out of the spectrum. Bit-exact.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB (guards formats.hpp's HLS headers)
#include "modules/spectral.hpp"            // the real module (uses GRID)
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace plalgo;                    // GRID, spectral_multiply

// Double re-derivation of one axis of compute_eField_DCT's spectral multiply.
static void golden(const std::vector<float>& a_uv, int axis, std::vector<double>& out) {
    const int N = GRID;
    const double TWO_PI = 2.0 * M_PI;
    out.assign((size_t)N * N, 0.0);
    for (int u = 0; u < N; u++) {
        const double w_u = TWO_PI * u / N;
        for (int v = 0; v < N; v++) {
            if (u == 0 && v == 0) { out[(size_t)u * N + v] = 0.0; continue; }   // DC drop
            const double w_v = TWO_PI * v / N;
            const double denom = w_u * w_u + w_v * w_v;
            const double coeff = (double)a_uv[(size_t)u * N + v];
            out[(size_t)u * N + v] = (axis == 0) ? coeff * w_u / denom
                                                 : coeff * w_v / denom;
        }
    }
}

static double rel_rms(const std::vector<float>& m, const std::vector<double>& g) {
    double se = 0, sr = 0;
    for (size_t i = 0; i < g.size(); i++) {
        const double e = (double)m[i] - g[i];
        se += e * e; sr += g[i] * g[i];
    }
    return std::sqrt(se / sr);
}

int main() {
    const int N = GRID;
    std::mt19937 rng(20260829u);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> a_uv((size_t)N * N);
    for (auto& v : a_uv) v = dist(rng);                 // signed density spectrum

    std::vector<float> ex((size_t)N * N, -7.7e30f), ey((size_t)N * N, -7.7e30f);
    spectral_multiply(a_uv.data(), ex.data(), 0);       // Ex_hat
    spectral_multiply(a_uv.data(), ey.data(), 1);       // Ey_hat

    std::vector<double> gx, gy;
    golden(a_uv, 0, gx);
    golden(a_uv, 1, gy);

    const double ex_rel = rel_rms(ex, gx), ey_rel = rel_rms(ey, gy);
    bool ok = true;
    const double TOL = 1e-6;    // float w_u/denom + one divide vs a double reference
    if (!(ex_rel < TOL)) { printf("FAIL [1] Ex rel_rms=%.3e (tol %.0e)\n", ex_rel, TOL); ok = false; }
    if (!(ey_rel < TOL)) { printf("FAIL [2] Ey rel_rms=%.3e (tol %.0e)\n", ey_rel, TOL); ok = false; }
    if (ex[0] != 0.0f || ey[0] != 0.0f) { printf("FAIL [3] DC not dropped: Ex[0]=%g Ey[0]=%g\n", ex[0], ey[0]); ok = false; }

    printf("[1] Ex (axis0) rel_rms=%.3e (tol %.0e)\n", ex_rel, TOL);
    printf("[2] Ey (axis1) rel_rms=%.3e (tol %.0e)\n", ey_rel, TOL);
    printf("[3] DC drop    Ex[0]=%g Ey[0]=%g (must be 0)\n", ex[0], ey[0]);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
