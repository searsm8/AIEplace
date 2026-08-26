#ifndef DCT_FFT_AIE_GOLDEN_HPP
#define DCT_FFT_AIE_GOLDEN_HPP

// golden.hpp -- double-precision reference for ONE dct_transpose pass, used to check the
// AIE-FFT hardware output on Geert's card. The device computes a fused "transform every
// row, then write transposed" pass; the reference here is exactly transpose(rows_xform(in)).
//
// Two independent definitions of each 1D transform are kept:
//   *_naive   -- the textbook summation (the definition; no FFT involved).
//   *_makhoul -- the Makhoul recipe the device implements (shuffle -> forward FFT -> twiddle).
// The device runs *_makhoul on real single-precision AIE cfloat FFT; this file runs both in
// double so the harness can (a) confirm makhoul == naive to ~1e-12 and (b) hand the host a
// trustworthy golden. The forward FFT is unnormalized (no 1/N), matching AIE DIR=1 TP_SHIFT=0.
//
// N is a runtime argument so the offline golden_test can exercise small grids; the on-device
// harness uses N = GRID = 1024.

#include <vector>
#include <complex>
#include <cmath>

namespace golden {

using cd  = std::complex<double>;
using Vec = std::vector<double>;
using Mat = std::vector<std::vector<double>>;

static const double PI = 3.14159265358979323846;

enum Xform { XF_DCT = 1, XF_IDCT = 2, XF_IDXST = 3 };   // matches formats.hpp TF_* values

// ---- reference complex FFT (iterative radix-2 DIT), sign=-1 forward, unnormalized -----
static inline void fft(std::vector<cd>& a, int sign) {
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

// ---- textbook 1D transforms (the definitions) -------------------------------
static inline Vec DCT_naive(const Vec& in) {
    int N = (int)in.size();
    Vec r(N);
    for (int k = 0; k < N; k++) {
        double s = 0;
        for (int n = 0; n < N; n++) s += in[n] * std::cos(PI / N * (n + 0.5) * k);
        r[k] = s;
    }
    return r;
}
static inline Vec IDCT_naive(const Vec& in) {
    int N = (int)in.size();
    Vec r(N);
    for (int k = 0; k < N; k++) {
        double s = 0;
        for (int n = 1; n < N; n++) s += in[n] * std::cos(PI / N * (k + 0.5) * n);
        r[k] = 0.5 * in[0] + s;
    }
    return r;
}
static inline Vec IDXST_naive(const Vec& in) {
    int N = (int)in.size();
    Vec t(N);
    t[0] = in[0];
    for (int n = 1; n < N; n++) t[n] = in[N - n];
    t = IDCT_naive(t);
    for (int n = 1; n < N; n += 2) t[n] *= -1;
    return t;
}

// ---- Makhoul recipe (what the device implements) ----------------------------
static inline Vec DCT_makhoul(const Vec& x) {
    int N = (int)x.size();
    std::vector<cd> v(N);
    for (int i = 0; i < N / 2; i++) v[i] = cd(x[2 * i], 0);
    for (int j = 0; j < N / 2; j++) v[N / 2 + j] = cd(x[N - 1 - 2 * j], 0);
    fft(v, -1);
    Vec out(N);
    for (int k = 0; k < N; k++) {
        cd tw = std::polar(1.0, -PI * k / (2.0 * N));   // e^{-i pi k / 2N}
        out[k] = std::real(v[k] * tw);
    }
    return out;
}
static inline Vec IDCT_makhoul(const Vec& X) {
    int N = (int)X.size();
    std::vector<cd> z(N);
    for (int n = 0; n < N; n++) {
        cd tw = std::polar(1.0, -PI * n / (2.0 * N));
        z[n] = cd(X[n], 0) * tw;
    }
    z[0] *= 0.5;
    fft(z, -1);
    Vec out(N);
    for (int i = 0; i < N / 2; i++) {
        out[2 * i]     = std::real(z[i]);
        out[2 * i + 1] = std::real(z[N - 1 - i]);
    }
    return out;
}
static inline Vec IDXST_makhoul(const Vec& X) {
    int N = (int)X.size();
    Vec r(N);
    r[0] = X[0];
    for (int i = 1; i < N; i++) r[i] = X[N - i];
    Vec y = IDCT_makhoul(r);
    for (int k = 1; k < N; k += 2) y[k] = -y[k];
    return y;
}

// ---- matrix helpers ---------------------------------------------------------
static inline Mat transpose(const Mat& A) {
    int R = (int)A.size(), C = (int)A[0].size();
    Mat T(C, Vec(R));
    for (int i = 0; i < R; i++) for (int j = 0; j < C; j++) T[j][i] = A[i][j];
    return T;
}
static inline Vec one_row(const Vec& row, int xform, bool makhoul) {
    if (xform == XF_DCT)   return makhoul ? DCT_makhoul(row)   : DCT_naive(row);
    if (xform == XF_IDCT)  return makhoul ? IDCT_makhoul(row)  : IDCT_naive(row);
    /* XF_IDXST */         return makhoul ? IDXST_makhoul(row) : IDXST_naive(row);
}
static inline Mat rows_xform(const Mat& A, int xform, bool makhoul) {
    Mat R; R.reserve(A.size());
    for (const auto& row : A) R.push_back(one_row(row, xform, makhoul));
    return R;
}

// The reference for ONE device pass: transform every row, then write transposed.
// Mirrors dct_transpose_pass(mat_in, mat_out, xform) in the PL module.
static inline Mat pass_golden(const Mat& in, int xform, bool makhoul = false) {
    return transpose(rows_xform(in, xform, makhoul));
}

static inline double rel_rms(const Mat& A, const Mat& B) {
    double e = 0, n = 0;
    for (size_t i = 0; i < A.size(); i++)
        for (size_t j = 0; j < A[i].size(); j++) {
            double d = A[i][j] - B[i][j];
            e += d * d; n += B[i][j] * B[i][j];
        }
    return std::sqrt(e / (n + 1e-30));
}

} // namespace golden

#endif // DCT_FFT_AIE_GOLDEN_HPP
