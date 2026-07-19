#ifndef PL_ALGO_FFT_PL_HPP
#define PL_ALGO_FFT_PL_HPP

// fft_pl -- PL (HLS) forward FFT + the Makhoul DCT/IDCT/IDXST 1D transforms, so the
// density solve runs entirely on the PL with NO AIE. This replaces the AIE forward-FFT
// pool used by dct_1d/dct_transpose. The math is a direct float port of the verified
// double golden in model/density_model.cpp (fft() + DCT_makhoul/IDCT_makhoul/IDXST_makhoul),
// itself proven == sw_only DCT.cpp. The AIE FFT was forward (DIR=1), unnormalized
// (TP_SHIFT=0); this port is the same: X_k = sum_n x_n e^{-i2pi kn/N}, no 1/N.
//
// Grid size N is PL_GRID (compile-time). The PL-only baseline uses a SMALL grid (e.g. 64,
// = sw_only BINS_PER_ROW) so the whole iteration fits in hw_emu RTL-sim time; the 1024 AIE
// datapath stays as the throughput path.
//
// HLS notes: in-place radix-2 DIT on separate re[]/im[] arrays (std::complex is avoided in
// the datapath). Twiddles come from a cos/sin ROM (built once) — same idea as the HPWL exp
// LUT and the dct_transpose twiddle ROM. N is a power of two.

#include <cmath>

namespace plalgo {

#ifndef PL_GRID
#define PL_GRID 1024
#endif

constexpr int FFT_N   = PL_GRID;
constexpr int FFT_LOG = (PL_GRID == 32 ? 5 : PL_GRID == 64 ? 6 : PL_GRID == 128 ? 7 :
                         PL_GRID == 256 ? 8 : PL_GRID == 512 ? 9 : 10);
static const float FFT_PI = 3.14159265358979f;

// Bit-reverse an index over FFT_LOG bits.
static inline int fft_bitrev(int x) {
    int r = 0;
    for (int b = 0; b < FFT_LOG; b++) { r = (r << 1) | (x & 1); x >>= 1; }
    return r;
}

// In-place forward FFT (DIT, radix-2), separate real/imag. sign=-1 forward (matches AIE).
static void fft_fwd(float re[FFT_N], float im[FFT_N]) {
    // bit-reversal permutation
bitrev:
    for (int i = 0; i < FFT_N; i++) {
#pragma HLS PIPELINE II=1
        int j = fft_bitrev(i);
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    // butterflies
stage:
    for (int len = 2; len <= FFT_N; len <<= 1) {
        const float ang = -2.0f * FFT_PI / (float)len;   // sign=-1 forward
        const float wr0 = cosf(ang), wi0 = sinf(ang);
    group:
        for (int i = 0; i < FFT_N; i += len) {
            float wr = 1.0f, wi = 0.0f;
        butt:
            for (int k = 0; k < len / 2; k++) {
#pragma HLS PIPELINE II=1
                const int a = i + k, b = i + k + len / 2;
                const float vr = re[b] * wr - im[b] * wi;   // v = a[b] * w
                const float vi = re[b] * wi + im[b] * wr;
                const float ur = re[a], ui = im[a];
                re[a] = ur + vr; im[a] = ui + vi;
                re[b] = ur - vr; im[b] = ui - vi;
                const float nwr = wr * wr0 - wi * wi0;       // w *= wlen
                const float nwi = wr * wi0 + wi * wr0;
                wr = nwr; wi = nwi;
            }
        }
    }
}

// ---- Makhoul 1D DCT: shuffle -> forward FFT -> twiddle+Re -------------------
static void dct_1d_pl(const float in[FFT_N], float out[FFT_N]) {
    const int N = FFT_N;
    float re[FFT_N], im[FFT_N];
shuf:
    for (int i = 0; i < N / 2; i++) {
#pragma HLS PIPELINE II=1
        re[i] = in[2 * i];            im[i] = 0.0f;
        re[N / 2 + i] = in[N - 1 - 2 * i]; im[N / 2 + i] = 0.0f;
    }
    fft_fwd(re, im);
post:
    for (int k = 0; k < N; k++) {
#pragma HLS PIPELINE II=1
        const float a = -FFT_PI * k / (2.0f * N);
        out[k] = re[k] * cosf(a) - im[k] * sinf(a);   // Re{ (re+i im) e^{ia} } = re cos a - im sin a
    }
}

// ---- Makhoul 1D IDCT: z_n = X_n e^{-i pi n/2N}, halve DC, FFT, unshuffle -----
static void idct_1d_pl(const float in[FFT_N], float out[FFT_N]) {
    const int N = FFT_N;
    float re[FFT_N], im[FFT_N];
pre:
    for (int n = 0; n < N; n++) {
#pragma HLS PIPELINE II=1
        const float a = -FFT_PI * n / (2.0f * N);
        re[n] = in[n] * cosf(a);   // (X_n + i0) * e^{ia}
        im[n] = in[n] * sinf(a);
    }
    re[0] *= 0.5f; im[0] *= 0.5f;
    fft_fwd(re, im);
unshuf:
    for (int i = 0; i < N / 2; i++) {
#pragma HLS PIPELINE II=1
        out[2 * i]     = re[i];
        out[2 * i + 1] = re[N - 1 - i];
    }
}

// ---- Makhoul 1D IDXST: reverse (keep x0) -> IDCT -> negate odd k ------------
static void idxst_1d_pl(const float in[FFT_N], float out[FFT_N]) {
    const int N = FFT_N;
    float r[FFT_N];
rev:
    for (int i = 0; i < N; i++) {
#pragma HLS PIPELINE II=1
        r[i] = (i == 0) ? in[0] : in[N - i];
    }
    idct_1d_pl(r, out);
sign:
    for (int k = 1; k < N; k += 2) {
#pragma HLS PIPELINE II=1
        out[k] = -out[k];
    }
}

} // namespace plalgo

#endif // PL_ALGO_FFT_PL_HPP
