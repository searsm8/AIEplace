// FieldVerify.cpp -- see FieldVerify.hpp.
//
// Old-ABI TU (like DCT1DVerify): host-side references live here; the new-ABI XRT Driver is
// reached only through the runXformTranspose/runSpectral/runField POD + const char* boundary.
// References are naive double-precision transforms matching the Stage 0 model (which proved
// makhoul == naive == markv1 compute_eField_DCT). Gate: rel_rms < 1e-3 (float32 1024-pt).

#include "FieldVerify.hpp"
#include "Driver.hpp"
#include "host_interface.hpp"

#include <vector>
#include <cmath>
#include <cstdio>
#include <random>

namespace plalgo {

static const double PI = 3.14159265358979323846;

// ---- naive 1D transform bases (precomputed NxN, row-major basis[k*N+n]) -----------------
// DCT_naive(x)[k]  = sum_n x[n] * cos(PI/N*(n+0.5)*k)
static std::vector<double> make_Cdct(int N) {
    std::vector<double> C((size_t)N * N);
    for (int k = 0; k < N; k++)
        for (int n = 0; n < N; n++)
            C[(size_t)k * N + n] = std::cos(PI / N * (n + 0.5) * k);
    return C;
}
// IDCT_naive(x)[k] = 0.5*x[0] + sum_{n>=1} x[n] * cos(PI/N*(k+0.5)*n)
//   => basis[k][n] = (n==0) ? 0.5 : cos(PI/N*(k+0.5)*n)
static std::vector<double> make_Cidct(int N) {
    std::vector<double> C((size_t)N * N);
    for (int k = 0; k < N; k++)
        for (int n = 0; n < N; n++)
            C[(size_t)k * N + n] = (n == 0) ? 0.5 : std::cos(PI / N * (k + 0.5) * n);
    return C;
}

// out[k] = sum_n basis[k*N+n] * x[n]   (one 1D transform of a length-N row)
static void matvec(const double* basis, const double* x, double* out, int N) {
    for (int k = 0; k < N; k++) {
        const double* b = &basis[(size_t)k * N];
        double s = 0;
        for (int n = 0; n < N; n++) s += b[n] * x[n];
        out[k] = s;
    }
}

// ---- matrix helpers (NxN, row-major double) --------------------------------------------
static void transpose(const std::vector<double>& A, std::vector<double>& T, int N) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            T[(size_t)j * N + i] = A[(size_t)i * N + j];
}
// apply DCT to each row; IDCT to each row; IDXST to each row (in place, A -> out)
static void dct_rows(const std::vector<double>& A, std::vector<double>& out,
                     const std::vector<double>& Cdct, int N) {
    for (int r = 0; r < N; r++)
        matvec(Cdct.data(), &A[(size_t)r * N], &out[(size_t)r * N], N);
}
static void idct_rows(const std::vector<double>& A, std::vector<double>& out,
                      const std::vector<double>& Cidct, int N) {
    for (int r = 0; r < N; r++)
        matvec(Cidct.data(), &A[(size_t)r * N], &out[(size_t)r * N], N);
}
// IDXST_naive(x): t[0]=x[0], t[n]=x[N-n]; t=IDCT_naive(t); negate odd outputs.
static void idxst_rows(const std::vector<double>& A, std::vector<double>& out,
                       const std::vector<double>& Cidct, int N) {
    std::vector<double> t(N);
    for (int r = 0; r < N; r++) {
        const double* x = &A[(size_t)r * N];
        t[0] = x[0];
        for (int n = 1; n < N; n++) t[n] = x[N - n];
        double* o = &out[(size_t)r * N];
        matvec(Cidct.data(), t.data(), o, N);
        for (int k = 1; k < N; k += 2) o[k] = -o[k];
    }
}

// spectral multiply (double): Ex_hat = a*w_u/denom, Ey_hat = a*w_v/denom, [0][0]=0.
static void spectral(const std::vector<double>& a_uv, std::vector<double>& Ex, std::vector<double>& Ey, int N) {
    const double TWO_PI = 2 * PI;
    for (int u = 0; u < N; u++) {
        const double w_u = TWO_PI * u / N;
        for (int v = 0; v < N; v++) {
            const double w_v = TWO_PI * v / N;
            const size_t idx = (size_t)u * N + v;
            if (u == 0 && v == 0) { Ex[idx] = 0; Ey[idx] = 0; continue; }
            const double denom = w_u * w_u + w_v * w_v;
            Ex[idx] = a_uv[idx] * w_u / denom;
            Ey[idx] = a_uv[idx] * w_v / denom;
        }
    }
}

// rel_rms(dev(float), ref(double)) over N*N, with max_abs.
static double rel_rms(const std::vector<float>& dev, const std::vector<double>& ref,
                      int N, double& max_abs) {
    double sse = 0, r = 0; max_abs = 0;
    for (size_t i = 0; i < (size_t)N * N; i++) {
        const double d = (double)dev[i] - ref[i];
        sse += d * d; r += ref[i] * ref[i];
        if (std::fabs(d) > max_abs) max_abs = std::fabs(d);
    }
    return std::sqrt(sse / (r + 1e-30));
}

// =========================================================================================
// 4a: fused IDCT+transpose vs transpose(IDCT_naive rows).
int runIdctTransposeVerify(const char* xclbin_path) {
    const int N = DENSITY_GRID;   // 1024

    std::mt19937 rng(23);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::vector<float> in((size_t)N * N);
    for (auto& v : in) v = uni(rng);
    std::vector<float> dev((size_t)N * N);

    printf("[idct_transpose] verify: N=%d lanes=%d (fused IDCT + transpose)\n", N, DENSITY_LANES);
    runXformTranspose(in.data(), N, (int)TFH_IDCT, dev.data(), xclbin_path);

    // Reference G[r][k] = IDCT_naive(in row r)[k]; expect dev[k][r] == G[r][k].
    const std::vector<double> Cidct = make_Cidct(N);
    std::vector<double> xr(N), gr(N);
    double sse = 0, ref = 0, max_abs = 0;
    for (int r = 0; r < N; r++) {
        for (int n = 0; n < N; n++) xr[n] = in[(size_t)r * N + n];
        matvec(Cidct.data(), xr.data(), gr.data(), N);
        for (int k = 0; k < N; k++) {
            const double d = (double)dev[(size_t)k * N + r] - gr[k];   // dev transposed: [k][r]
            sse += d * d; ref += gr[k] * gr[k];
            if (std::fabs(d) > max_abs) max_abs = std::fabs(d);
        }
    }
    const double rr = std::sqrt(sse / (ref + 1e-30));
    const bool   ok = rr < 1e-3;
    printf("[idct_transpose] fused IDCT+transpose vs transpose(IDCT_naive): max_abs=%.3e  "
           "rel_rms=%.3e  -> %s\n", max_abs, rr, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// =========================================================================================
// 4b: fused IDXST+transpose vs transpose(IDXST_naive rows).
int runIdxstTransposeVerify(const char* xclbin_path) {
    const int N = DENSITY_GRID;

    std::mt19937 rng(29);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::vector<float> in((size_t)N * N);
    for (auto& v : in) v = uni(rng);
    std::vector<float> dev((size_t)N * N);

    printf("[idxst_transpose] verify: N=%d lanes=%d (fused IDXST + transpose)\n", N, DENSITY_LANES);
    runXformTranspose(in.data(), N, (int)TFH_IDXST, dev.data(), xclbin_path);

    // Reference G[r][k] = IDXST_naive(in row r)[k]; expect dev[k][r] == G[r][k].
    const std::vector<double> Cidct = make_Cidct(N);
    std::vector<double> tr(N), gr(N);
    double sse = 0, ref = 0, max_abs = 0;
    for (int r = 0; r < N; r++) {
        const float* x = &in[(size_t)r * N];
        tr[0] = x[0];
        for (int n = 1; n < N; n++) tr[n] = x[N - n];
        matvec(Cidct.data(), tr.data(), gr.data(), N);
        for (int k = 1; k < N; k += 2) gr[k] = -gr[k];
        for (int k = 0; k < N; k++) {
            const double d = (double)dev[(size_t)k * N + r] - gr[k];   // dev transposed: [k][r]
            sse += d * d; ref += gr[k] * gr[k];
            if (std::fabs(d) > max_abs) max_abs = std::fabs(d);
        }
    }
    const double rr = std::sqrt(sse / (ref + 1e-30));
    const bool   ok = rr < 1e-3;
    printf("[idxst_transpose] fused IDXST+transpose vs transpose(IDXST_naive): max_abs=%.3e  "
           "rel_rms=%.3e  -> %s\n", max_abs, rr, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// =========================================================================================
// 4c: spectral multiply (both fields) vs the golden formula.
int runSpectralVerify(const char* xclbin_path) {
    const int N = DENSITY_GRID;

    std::mt19937 rng(31);
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
    std::vector<float> a_uv((size_t)N * N);
    for (auto& v : a_uv) v = uni(rng);
    std::vector<float> devEx((size_t)N * N), devEy((size_t)N * N);

    printf("[spectral] verify: N=%d (a_uv -> Ex_hat, Ey_hat)\n", N);
    runSpectral(a_uv.data(), N, devEx.data(), devEy.data(), xclbin_path);

    std::vector<double> ad((size_t)N * N), refEx((size_t)N * N), refEy((size_t)N * N);
    for (size_t i = 0; i < (size_t)N * N; i++) ad[i] = a_uv[i];
    spectral(ad, refEx, refEy, N);

    double maEx, maEy;
    const double rrEx = rel_rms(devEx, refEx, N, maEx);
    const double rrEy = rel_rms(devEy, refEy, N, maEy);
    const bool   ok   = rrEx < 1e-3 && rrEy < 1e-3;
    printf("[spectral] Ex_hat: max_abs=%.3e rel_rms=%.3e | Ey_hat: max_abs=%.3e rel_rms=%.3e -> %s\n",
           maEx, rrEx, maEy, rrEy, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// =========================================================================================
// 4d: full field solve rho -> Ex, Ey vs the naive compute_eField_DCT pipeline.
int runFieldVerify(const char* xclbin_path) {
    const int N = DENSITY_GRID;

    std::mt19937 rng(37);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::vector<float> rho((size_t)N * N);
    for (auto& v : rho) v = uni(rng);
    std::vector<float> devEx((size_t)N * N), devEy((size_t)N * N);

    printf("[field] verify: N=%d lanes=%d (rho -> Ex, Ey via the full field solve)\n", N, DENSITY_LANES);
    runField(rho.data(), N, devEx.data(), devEy.data(), xclbin_path);

    // ---- naive double reference, mirroring compute_a_uv_DCT + compute_eField_DCT ----
    const std::vector<double> Cdct  = make_Cdct(N);
    const std::vector<double> Cidct = make_Cidct(N);
    std::vector<double> A((size_t)N * N), B((size_t)N * N), a_uv((size_t)N * N);
    std::vector<double> Ex((size_t)N * N), Ey((size_t)N * N), Ex_hat((size_t)N * N), Ey_hat((size_t)N * N);
    for (size_t i = 0; i < (size_t)N * N; i++) A[i] = rho[i];

    // forward 2D DCT: a_uv = transpose(DCT_rows(transpose(DCT_rows(rho))))
    dct_rows(A, B, Cdct, N);        transpose(B, A, N);
    dct_rows(A, B, Cdct, N);        transpose(B, a_uv, N);

    // spectral multiply
    spectral(a_uv, Ex_hat, Ey_hat, N);

    // inverse: Ex = transpose(IDXST_rows(transpose(IDCT_rows(Ex_hat))))
    idct_rows (Ex_hat, A, Cidct, N); transpose(A, B, N); idxst_rows(B, A, Cidct, N); transpose(A, Ex, N);
    // Ey = transpose(IDCT_rows(transpose(IDXST_rows(Ey_hat))))
    idxst_rows(Ey_hat, A, Cidct, N); transpose(A, B, N); idct_rows (B, A, Cidct, N); transpose(A, Ey, N);

    double maEx, maEy;
    const double rrEx = rel_rms(devEx, Ex, N, maEx);
    const double rrEy = rel_rms(devEy, Ey, N, maEy);
    const bool   ok   = rrEx < 1e-3 && rrEy < 1e-3;
    printf("[field] Ex: max_abs=%.3e rel_rms=%.3e | Ey: max_abs=%.3e rel_rms=%.3e -> %s\n",
           maEx, rrEx, maEy, rrEy, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace plalgo
