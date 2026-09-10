// golden_test.cpp -- validate the double-precision reference in host/golden.hpp BEFORE the
// hardware harness relies on it. Pure g++, no Vitis/XRT. Asserts and exits non-zero on any
// failure (repo rule: a test asserts, it does not print numbers for a human to eyeball).
//
//   build+run: g++ -O2 -I../src/host golden_test.cpp -o golden_test && ./golden_test
//   (the package Makefile wires this as `make golden`.)
//
// Checks, each independent of the FFT machinery:
//   A  DCT of a constant row is analytic: [N, 0, 0, ...]           (catches scale/sign/index)
//   B  the Makhoul recipe == the textbook definition, ~1e-11       (both must be right)
//   C  IDCT_naive(DCT_naive(x)) == (N/2) x                          (DCT-II/III round trip)
//   D  one device pass on a constant matrix is analytic, and the
//      makhoul pass == the naive pass                               (the shipped golden)

#include "golden.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace golden;

static int failures = 0;
static void check(bool ok, const char* name, double got, double tol) {
    if (!ok) { std::printf("FAIL %-28s value=%.3e tol=%.3e\n", name, got, tol); failures++; }
}

static double max_abs_diff(const Vec& a, const Vec& b) {
    double m = 0;
    for (size_t i = 0; i < a.size(); i++) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

int main() {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);

    // ---- A: DCT of a constant row -> [N, 0, ...] ----------------------------
    for (int N : {32, 256, 1024}) {
        Vec ones(N, 1.0);
        Vec d = DCT_naive(ones);
        double err = std::fabs(d[0] - (double)N);
        for (int k = 1; k < N; k++) err = std::max(err, std::fabs(d[k]));
        check(err < 1e-9 * N, "A dct(const) analytic", err, 1e-9 * N);
    }

    // ---- B: Makhoul recipe == textbook definition --------------------------
    for (int N : {32, 256, 1024}) {
        Vec x(N);
        for (double& v : x) v = uni(rng);
        double eps = 1e-9 * N;   // grows mildly with N (accumulated double round-off)
        check(max_abs_diff(DCT_makhoul(x),   DCT_naive(x))   < eps, "B dct makhoul==naive",   max_abs_diff(DCT_makhoul(x),   DCT_naive(x)),   eps);
        check(max_abs_diff(IDCT_makhoul(x),  IDCT_naive(x))  < eps, "B idct makhoul==naive",  max_abs_diff(IDCT_makhoul(x),  IDCT_naive(x)),  eps);
        check(max_abs_diff(IDXST_makhoul(x), IDXST_naive(x)) < eps, "B idxst makhoul==naive", max_abs_diff(IDXST_makhoul(x), IDXST_naive(x)), eps);
    }

    // ---- C: IDCT_naive(DCT_naive(x)) == (N/2) x ----------------------------
    for (int N : {32, 256}) {
        Vec x(N);
        for (double& v : x) v = uni(rng);
        Vec rt = IDCT_naive(DCT_naive(x));
        Vec scaled(N);
        for (int i = 0; i < N; i++) scaled[i] = 0.5 * N * x[i];
        double err = max_abs_diff(rt, scaled);
        check(err < 1e-9 * N, "C dct/idct round trip", err, 1e-9 * N);
    }

    // ---- D: one device pass, constant matrix, analytic + makhoul==naive ----
    for (int N : {32, 256}) {
        Mat in(N, Vec(N, 1.0));                       // all-ones NxN
        Mat out = pass_golden(in, XF_DCT, /*makhoul=*/false);
        // dct of each const row -> [N,0,..]; transpose -> row 0 all N, rest 0.
        double err = 0;
        for (int r = 0; r < N; r++)
            for (int c = 0; c < N; c++)
                err = std::max(err, std::fabs(out[r][c] - (r == 0 ? (double)N : 0.0)));
        check(err < 1e-9 * N, "D pass(const) analytic", err, 1e-9 * N);

        // random matrix: the shipped makhoul golden must equal the naive golden.
        Mat rnd(N, Vec(N));
        for (auto& row : rnd) for (double& v : row) v = uni(rng);
        for (int xf : {XF_DCT, XF_IDCT, XF_IDXST}) {
            double rr = rel_rms(pass_golden(rnd, xf, true), pass_golden(rnd, xf, false));
            check(rr < 1e-11, "D pass makhoul==naive", rr, 1e-11);
        }
    }

    if (failures) { std::printf("golden_test: %d FAILURE(S)\n", failures); return 1; }
    std::printf("golden_test: PASS (all checks)\n");
    return 0;
}
