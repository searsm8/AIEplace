// Verify fft_pl.hpp (float PL DCT/IDCT/IDXST) vs the naive golden transforms.
#define PL_GRID 64
#include "modules/fft_pl.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
#include <random>

static const double PI = 3.14159265358979323846;
static std::vector<double> DCT_naive(const std::vector<double>& in) {
    int N = in.size(); std::vector<double> r(N);
    for (int k = 0; k < N; k++) { double s = 0; for (int n = 0; n < N; n++) s += in[n]*std::cos(PI/N*(n+0.5)*k); r[k]=s; }
    return r;
}
static std::vector<double> IDCT_naive(const std::vector<double>& in) {
    int N = in.size(); std::vector<double> r(N);
    for (int k = 0; k < N; k++) { double s = 0; for (int n = 1; n < N; n++) s += in[n]*std::cos(PI/N*(k+0.5)*n); r[k]=0.5*in[0]+s; }
    return r;
}
static std::vector<double> IDXST_naive(const std::vector<double>& in) {
    int N = in.size(); std::vector<double> t(N); t[0]=in[0];
    for (int n=1;n<N;n++) t[n]=in[N-n]; t=IDCT_naive(t);
    for (int n=1;n<N;n+=2) t[n]*=-1; return t;
}
static double rel_rms(const float* a, const std::vector<double>& b) {
    double e=0,n=0; for (size_t i=0;i<b.size();i++){double d=a[i]-b[i]; e+=d*d; n+=b[i]*b[i];} return std::sqrt(e/(n+1e-30));
}

int main() {
    const int N = PL_GRID;
    std::mt19937 rng(12345); std::uniform_real_distribution<double> uni(0,1);
    std::vector<double> xd(N); float xf[PL_GRID];
    for (int i=0;i<N;i++){ xd[i]=uni(rng); xf[i]=(float)xd[i]; }
    float dct[PL_GRID], idct[PL_GRID], idxst[PL_GRID];
    plalgo::dct_1d_pl(xf, dct);
    plalgo::idct_1d_pl(xf, idct);
    plalgo::idxst_1d_pl(xf, idxst);
    double e_dct = rel_rms(dct, DCT_naive(xd));
    double e_idct = rel_rms(idct, IDCT_naive(xd));
    double e_idxst = rel_rms(idxst, IDXST_naive(xd));
    printf("N=%d  DCT rel_rms=%.3e  IDCT=%.3e  IDXST=%.3e\n", N, e_dct, e_idct, e_idxst);

    // float radix-2 FFT vs double naive; observed worst ~3.9e-07 (IDXST).
    const double TOL = 1e-6;
    double worst = std::fmax(e_dct, std::fmax(e_idct, e_idxst));
    bool ok = worst < TOL;
    printf("%s  (worst rel_rms=%.3e, tol %.0e)\n", ok ? "PASS" : "FAIL", worst, TOL);
    return ok ? 0 : 1;
}
