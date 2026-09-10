// DCT1DVerify.cpp -- see DCT1DVerify.hpp.
//
// Old-ABI TU (like HpwlGradVerify/DensityVerify): computes the host-side references
// here and crosses into the new-ABI XRT Driver only through runDCT1D's POD + const
// char* boundary. The reference FFT uses the SAME convention as the AIE FFT (forward,
// unnormalized, X_k = sum_n x_n e^{-i2pi kn/N}); DCT_naive matches the Stage 0 model.

#include "DCT1DVerify.hpp"
#include "Driver.hpp"
#include "host_interface.hpp"

#include <vector>
#include <complex>
#include <cmath>
#include <cstdio>
#include <random>
#include <algorithm>

namespace plalgo {

using cd = std::complex<double>;
static const double PI = 3.14159265358979323846;

// Reference forward FFT (iterative radix-2 DIT), matching the AIE FFT convention.
static void ref_fft(std::vector<cd>& a) {
    int n = (int)a.size();
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2 * PI / len;                 // forward
        cd wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            cd w(1, 0);
            for (int k = 0; k < len / 2; k++) {
                cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wlen;
            }
        }
    }
}

// sw_only golden DCT (DCT.cpp DCT_naive), unnormalized.
static std::vector<double> ref_dct(const float* x, int N) {
    std::vector<double> r(N);
    for (int k = 0; k < N; k++) {
        double s = 0;
        for (int n = 0; n < N; n++) s += (double)x[n] * std::cos(PI / N * (n + 0.5) * k);
        r[k] = s;
    }
    return r;
}

int runDCT1DVerify(const char* xclbin_path) {
    const int N = FFT_PTS;     // 1024
    const int K = 4;           // test vectors (frames)

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::vector<float> in((size_t)K * N);
    for (auto& v : in) v = uni(rng);

    std::vector<float> out_fft((size_t)K * N * 2);   // stage 0: complex {re,im}
    std::vector<float> out_dct((size_t)K * N);       // stage 1: real DCT

    printf("[dct1d] verify: frames=%d N=%d (single AIE FFT lane)\n", K, N);
    runDCT1D(in.data(), K, out_fft.data(), out_dct.data(), xclbin_path);

    const double TOL = 1e-3;   // float32 1024-pt FFT vs double reference

    // ---- stage 0: raw AIE FFT vs reference forward FFT ----
    double sse0 = 0, ref0 = 0;
    for (int f = 0; f < K; f++) {
        std::vector<cd> x(N);
        for (int i = 0; i < N; i++) x[i] = cd(in[(size_t)f * N + i], 0);
        ref_fft(x);
        for (int k = 0; k < N; k++) {
            const double dre = out_fft[2 * ((size_t)f * N + k)]     - x[k].real();
            const double dim = out_fft[2 * ((size_t)f * N + k) + 1] - x[k].imag();
            sse0 += dre * dre + dim * dim;
            ref0 += x[k].real() * x[k].real() + x[k].imag() * x[k].imag();
        }
    }
    const double rr0  = std::sqrt(sse0 / (ref0 + 1e-30));
    const bool   ok0  = rr0 < TOL;
    printf("[dct1d] stage0 FFT-passthrough vs ref FFT : rel_rms=%.3e -> %s\n",
           rr0, ok0 ? "PASS" : "FAIL");

    // ---- stage 1: full DCT vs DCT_naive ----
    double sse1 = 0, ref1 = 0;
    for (int f = 0; f < K; f++) {
        std::vector<double> g = ref_dct(&in[(size_t)f * N], N);
        for (int k = 0; k < N; k++) {
            const double d = out_dct[(size_t)f * N + k] - g[k];
            sse1 += d * d; ref1 += g[k] * g[k];
        }
    }
    const double rr1  = std::sqrt(sse1 / (ref1 + 1e-30));
    const bool   ok1  = rr1 < TOL;
    printf("[dct1d] stage1 DCT vs DCT_naive          : rel_rms=%.3e -> %s\n",
           rr1, ok1 ? "PASS" : "FAIL");

    return (ok0 && ok1) ? 0 : 1;
}

int runDCTRowPassVerify(const char* xclbin_path) {
    const int N = FFT_PTS;          // 1024
    const int R = 64;               // rows (multiple of DENSITY_LANES); 8 batches of 8

    std::mt19937 rng(11);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::vector<float> in((size_t)R * N);
    for (auto& v : in) v = uni(rng);
    std::vector<float> dev((size_t)R * N);

    printf("[dct_rowpass] verify: rows=%d N=%d lanes=%d\n", R, N, DENSITY_LANES);
    runDCTRowPass(in.data(), R, dev.data(), xclbin_path);

    double sse = 0, ref = 0, max_abs = 0;
    for (int r = 0; r < R; r++) {
        std::vector<double> g = ref_dct(&in[(size_t)r * N], N);
        for (int k = 0; k < N; k++) {
            const double d = dev[(size_t)r * N + k] - g[k];
            sse += d * d; ref += g[k] * g[k];
            if (std::fabs(d) > max_abs) max_abs = std::fabs(d);
        }
    }
    const double rr = std::sqrt(sse / (ref + 1e-30));
    const bool   ok = rr < 1e-3;
    printf("[dct_rowpass] DCT all rows vs DCT_naive: max_abs=%.3e  rel_rms=%.3e  -> %s\n",
           max_abs, rr, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int runDctTransposeVerify(const char* xclbin_path) {
    const int N = FFT_PTS;          // 1024 (square GRID x GRID -- transpose needs square)

    std::mt19937 rng(13);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::vector<float> in((size_t)N * N);
    for (auto& v : in) v = uni(rng);
    std::vector<float> dev((size_t)N * N);

    printf("[dct_transpose] verify: N=%d lanes=%d (fused DCT + transpose)\n", N, DENSITY_LANES);
    runDctTranspose(in.data(), N, dev.data(), xclbin_path);

    // Reference = transpose(rowDCT(in)): G[r][k] = DCT_naive(in row r)[k]; expect
    // dev[k][r] == G[r][k]. Precompute the DCT basis once (cos in the inner loop over the
    // full N x N x N would be minutes; the basis makes the compare an O(N^3) mult-add).
    std::vector<double> basis((size_t)N * N);   // basis[k*N + n] = cos(pi/N*(n+0.5)*k)
    for (int k = 0; k < N; k++)
        for (int n = 0; n < N; n++)
            basis[(size_t)k * N + n] = std::cos(PI / N * (n + 0.5) * k);

    double sse = 0, ref = 0, max_abs = 0;
    for (int r = 0; r < N; r++) {
        const float* row = &in[(size_t)r * N];
        for (int k = 0; k < N; k++) {
            double g = 0;
            const double* b = &basis[(size_t)k * N];
            for (int n = 0; n < N; n++) g += (double)row[n] * b[n];   // DCT_naive(row)[k]
            const double d = dev[(size_t)k * N + r] - g;              // dev is transposed: [k][r]
            sse += d * d; ref += g * g;
            if (std::fabs(d) > max_abs) max_abs = std::fabs(d);
        }
    }
    const double rr = std::sqrt(sse / (ref + 1e-30));
    const bool   ok = rr < 1e-3;
    printf("[dct_transpose] fused DCT+transpose vs transpose(DCT_naive): max_abs=%.3e  "
           "rel_rms=%.3e  -> %s\n", max_abs, rr, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int runAuvVerify(const char* xclbin_path) {
    const int N = FFT_PTS;          // 1024 (square GRID x GRID)

    std::mt19937 rng(17);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::vector<float> rho((size_t)N * N);
    for (auto& v : rho) v = uni(rng);
    std::vector<float> dev((size_t)N * N);

    printf("[auv] verify: N=%d lanes=%d (fwd 2D DCT = two fused passes)\n", N, DENSITY_LANES);
    runDct2D(rho.data(), N, dev.data(), xclbin_path);

    // Reference = separable 2D DCT, a[p][q] = sum_{m,n} C[p][m] rho[m][n] C[q][n] = C*rho*C^T
    // (same C basis as the 1D refs). Two O(N^3) passes; loops kept cache-friendly.
    std::vector<double> C((size_t)N * N);   // C[k*N + n] = cos(pi/N*(n+0.5)*k)
    for (int k = 0; k < N; k++)
        for (int n = 0; n < N; n++)
            C[(size_t)k * N + n] = std::cos(PI / N * (n + 0.5) * k);

    // R[m][q] = sum_n rho[m][n] C[q][n]  (DCT along n)
    std::vector<double> R((size_t)N * N);
    for (int m = 0; m < N; m++) {
        const float* rrow = &rho[(size_t)m * N];
        for (int q = 0; q < N; q++) {
            const double* cq = &C[(size_t)q * N];
            double s = 0;
            for (int n = 0; n < N; n++) s += (double)rrow[n] * cq[n];
            R[(size_t)m * N + q] = s;
        }
    }

    // a[p][q] = sum_m C[p][m] R[m][q]  (DCT along m); compare dev[p*N + q]
    std::vector<double> arow(N);
    double sse = 0, ref = 0, max_abs = 0;
    for (int p = 0; p < N; p++) {
        std::fill(arow.begin(), arow.end(), 0.0);
        const double* cp = &C[(size_t)p * N];
        for (int m = 0; m < N; m++) {
            const double  cpm = cp[m];
            const double* rm  = &R[(size_t)m * N];
            for (int q = 0; q < N; q++) arow[q] += cpm * rm[q];
        }
        for (int q = 0; q < N; q++) {
            const double d = dev[(size_t)p * N + q] - arow[q];
            sse += d * d; ref += arow[q] * arow[q];
            if (std::fabs(d) > max_abs) max_abs = std::fabs(d);
        }
    }
    const double rr = std::sqrt(sse / (ref + 1e-30));
    const bool   ok = rr < 1e-3;
    printf("[auv] fwd 2D DCT vs separable C*rho*C^T: max_abs=%.3e  rel_rms=%.3e  -> %s\n",
           max_abs, rr, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace plalgo
