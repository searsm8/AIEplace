// density_model.cpp -- pl_algo density_manager Stage 0 software math model.
//
// Purpose: prove, in pure host C++ (no HLS, no AIE), that the Makhoul
// "shuffle + forward-FFT + twiddle" recipe ported from the markv1 AIE kernels
// reproduces the markv1 CPU golden transforms exactly, in 1D and then in the
// full 2D electrostatic-field pipeline. This pins down the FFT normalization and
// the DC-halving convention in software before any slow HLS/AIE build.
//
// Golden = the DCT-PATH functions from host/src/markv1/src/DCT.cpp +
// the pipeline of Density.cpp::compute_a_uv_DCT / compute_eField_DCT
// (NOT the *_naive direct-sum field path, NOT *_naive with 1/N). Global scale is
// absorbed by density_weight downstream, so we gate on rel_rms vs this golden.
//
// The AIE FFT is forward (DIR=1), cfloat, unnormalized (TP_SHIFT=0); all three
// markv1 graphs (DCT/IDCT/IDXST) use that SAME forward FFT and differ only in the
// PL pre/post. This model uses double-precision complex math: Stage 0 proves the
// MATH mapping is exact; float/cfloat rounding is validated later in hardware.
//
// Build + run (WSL):  g++ -O2 -std=c++17 density_model.cpp -o density_model && ./density_model

#include <vector>
#include <complex>
#include <cmath>
#include <cstdio>
#include <random>

using cd = std::complex<double>;
static const double PI = 3.14159265358979323846;

// ---- reference complex FFT (iterative radix-2 DIT) -------------------------
// sign=-1: forward, X_k = sum_n x_n e^{-i2pi kn/N}  (matches AIE DIR=1, unnormalized).
// sign=+1: inverse kernel (no 1/N). The markv1 recipe only uses the forward FFT.
static void fft(std::vector<cd>& a, int sign) {
    int n = (int)a.size();
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = sign * 2 * PI / len;
        cd wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            cd w(1, 0);
            for (int k = 0; k < len / 2; k++) {
                cd u = a[i + k];
                cd v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ---- markv1 CPU golden transforms (copied verbatim from DCT.cpp) -----------
static std::vector<double> DCT_naive(const std::vector<double>& in) {
    int N = (int)in.size();
    std::vector<double> r(N);
    for (int k = 0; k < N; k++) {
        double s = 0;
        for (int n = 0; n < N; n++) s += in[n] * std::cos(PI / N * (n + 0.5) * k);
        r[k] = s;
    }
    return r;
}
static std::vector<double> IDCT_naive(const std::vector<double>& in) {
    int N = (int)in.size();
    std::vector<double> r(N);
    for (int k = 0; k < N; k++) {
        double s = 0;
        for (int n = 1; n < N; n++) s += in[n] * std::cos(PI / N * (k + 0.5) * n);
        r[k] = 0.5 * in[0] + s;
    }
    return r;
}
static std::vector<double> IDXST_naive(const std::vector<double>& in) {
    int N = (int)in.size();
    std::vector<double> t(N);
    t[0] = in[0];
    for (int n = 1; n < N; n++) t[n] = in[N - n];
    t = IDCT_naive(t);
    for (int n = 1; n < N; n += 2) t[n] *= -1;
    return t;
}

// ---- Makhoul recipe ported from the markv1 AIE pre/post kernels ------------
// dct_shuffle:   {x0,x2,x4,...,x_{N-2}, x_{N-1},x_{N-3},...,x1}
// dct_postprocess: out_k = Re{ FFT_k * e^{-i pi k / 2N} }
static std::vector<double> DCT_makhoul(const std::vector<double>& x) {
    int N = (int)x.size();
    std::vector<cd> v(N);
    for (int i = 0; i < N / 2; i++) v[i] = cd(x[2 * i], 0);
    for (int j = 0; j < N / 2; j++) v[N / 2 + j] = cd(x[N - 1 - 2 * j], 0);
    fft(v, -1);
    std::vector<double> out(N);
    for (int k = 0; k < N; k++) {
        cd tw = std::polar(1.0, -PI * k / (2.0 * N));   // e^{-i pi k / 2N}
        out[k] = std::real(v[k] * tw);
    }
    return out;
}
// idct_preprocess: z_n = X_n * e^{-i pi n / 2N}, then halve z_0 (DC).
// forward FFT, then idct_unshuffle: out[2i]=Re(F_i), out[2i+1]=Re(F_{N-1-i}).
static std::vector<double> IDCT_makhoul(const std::vector<double>& X) {
    int N = (int)X.size();
    std::vector<cd> z(N);
    for (int n = 0; n < N; n++) {
        cd tw = std::polar(1.0, -PI * n / (2.0 * N));
        z[n] = cd(X[n], 0) * tw;
    }
    z[0] *= 0.5;
    fft(z, -1);
    std::vector<double> out(N);
    for (int i = 0; i < N / 2; i++) {
        out[2 * i]     = std::real(z[i]);
        out[2 * i + 1] = std::real(z[N - 1 - i]);
    }
    return out;
}
// idxst_shuffle (reverse, keep x0) -> IDCT path -> idxst_signs (negate odd k).
static std::vector<double> IDXST_makhoul(const std::vector<double>& X) {
    int N = (int)X.size();
    std::vector<double> r(N);
    r[0] = X[0];
    for (int i = 1; i < N; i++) r[i] = X[N - i];
    std::vector<double> y = IDCT_makhoul(r);
    for (int k = 1; k < N; k += 2) y[k] = -y[k];
    return y;
}

// ---- helpers ---------------------------------------------------------------
using Mat = std::vector<std::vector<double>>;
static double rel_rms(const std::vector<double>& a, const std::vector<double>& b) {
    double e = 0, n = 0;
    for (size_t i = 0; i < a.size(); i++) { double d = a[i] - b[i]; e += d * d; n += b[i] * b[i]; }
    return std::sqrt(e / (n + 1e-30));
}
static double rel_rms(const Mat& A, const Mat& B) {
    double e = 0, n = 0;
    for (size_t i = 0; i < A.size(); i++)
        for (size_t j = 0; j < A[i].size(); j++) { double d = A[i][j] - B[i][j]; e += d * d; n += B[i][j] * B[i][j]; }
    return std::sqrt(e / (n + 1e-30));
}
static Mat transpose(const Mat& A) {
    int R = (int)A.size(), C = (int)A[0].size();
    Mat T(R, std::vector<double>(C));
    for (int i = 0; i < R; i++) for (int j = 0; j < C; j++) T[i][j] = A[j][i];
    return T; // square only (our grid is NxN)
}
static Mat dct_rows(const Mat& A, std::vector<double>(*f)(const std::vector<double>&)) {
    Mat R; R.reserve(A.size());
    for (auto& row : A) R.push_back(f(row));
    return R;
}

// ---- 2D pipeline (mirrors compute_a_uv_DCT + compute_eField_DCT) -----------
// a_uv = DCT_x(DCT_y(rho)) : DCT rows, transpose, DCT rows, transpose.
static Mat forward_a_uv(const Mat& rho, std::vector<double>(*dct)(const std::vector<double>&)) {
    return transpose(dct_rows(transpose(dct_rows(rho, dct)), dct));
}
// spectral multiply: Ex_hat=a_uv*w_u/denom, Ey_hat=a_uv*w_v/denom, [0][0]=0.
static void spectral(const Mat& a_uv, Mat& Ex, Mat& Ey) {
    int N = (int)a_uv.size();
    Ex.assign(N, std::vector<double>(N, 0));
    Ey.assign(N, std::vector<double>(N, 0));
    for (int u = 0; u < N; u++)
        for (int v = 0; v < N; v++) {
            if (u == 0 && v == 0) continue;
            double w_u = 2 * PI * u / N, w_v = 2 * PI * v / N;
            double denom = w_u * w_u + w_v * w_v;
            Ex[u][v] = a_uv[u][v] * w_u / denom;
            Ey[u][v] = a_uv[u][v] * w_v / denom;
        }
}
// inverse: Ex = IDXST_x o IDCT_y ; Ey = IDCT_x o IDXST_y  (with a transpose between axes).
static void inverse_fields(Mat& Ex, Mat& Ey,
                           std::vector<double>(*idct)(const std::vector<double>&),
                           std::vector<double>(*idxst)(const std::vector<double>&)) {
    Ex = dct_rows(Ex, idct);   Ey = dct_rows(Ey, idxst);   // along y
    Ex = transpose(Ex);        Ey = transpose(Ey);
    Ex = dct_rows(Ex, idxst);  Ey = dct_rows(Ey, idct);    // along x
    Ex = transpose(Ex);        Ey = transpose(Ey);
}

int main() {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    printf("== Stage 0: density math model (Makhoul recipe vs markv1 golden) ==\n\n");

    // ---- 1D tests (N=1024 HW size, and N=64 markv1 BINS_PER_ROW) ----
    printf("1D transforms (rel_rms = ||makhoul - naive|| / ||naive||):\n");
    for (int N : {64, 1024}) {
        std::vector<double> x(N);
        for (auto& xi : x) xi = uni(rng);
        double d_dct  = rel_rms(DCT_makhoul(x),  DCT_naive(x));
        double d_idct = rel_rms(IDCT_makhoul(x), IDCT_naive(x));
        double d_idxs = rel_rms(IDXST_makhoul(x),IDXST_naive(x));
        printf("  N=%-5d  DCT=%.3e  IDCT=%.3e  IDXST=%.3e\n", N, d_dct, d_idct, d_idxs);
    }

    // ---- 2D field pipeline (N=64, matches markv1 BINS_PER_ROW) ----
    // golden = naive transforms; test = makhoul transforms; same pipeline structure.
    int N = 64;
    Mat rho(N, std::vector<double>(N));
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) rho[i][j] = uni(rng); // synthetic density

    Mat auv_g  = forward_a_uv(rho, DCT_naive);
    Mat auv_m  = forward_a_uv(rho, DCT_makhoul);
    Mat ExG, EyG, ExM, EyM;
    spectral(auv_g, ExG, EyG); inverse_fields(ExG, EyG, IDCT_naive,  IDXST_naive);
    spectral(auv_m, ExM, EyM); inverse_fields(ExM, EyM, IDCT_makhoul,IDXST_makhoul);

    printf("\n2D pipeline (N=%d, rel_rms makhoul vs naive golden):\n", N);
    printf("  a_uv=%.3e   Ex=%.3e   Ey=%.3e\n",
           rel_rms(auv_m, auv_g), rel_rms(ExM, ExG), rel_rms(EyM, EyG));

    printf("\nPASS if all rel_rms ~ 1e-12 or below (double-precision mapping is exact).\n");
    return 0;
}
