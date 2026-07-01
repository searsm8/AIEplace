// TransposeVerify.cpp -- see TransposeVerify.hpp.

#include "TransposeVerify.hpp"
#include "Driver.hpp"
#include "host_interface.hpp"

#include <vector>
#include <cstdio>
#include <cmath>
#include <random>

namespace plalgo {

// A transpose is exact data movement -> compare BIT-EXACT vs the host transpose
// gold: out[c][r] == in[r][c].
static int check_bitexact(const char* name, const std::vector<float>& dev,
                          const std::vector<float>& in, int N) {
    long   mism = 0;
    double max_abs = 0;
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) {
            const float g = in[(size_t)r * N + c];      // gold transpose: dev[c][r] should == in[r][c]
            const float d = dev[(size_t)c * N + r];
            if (d != g) { mism++; double e = std::fabs((double)d - g); if (e > max_abs) max_abs = e; }
        }
    const bool ok = (mism == 0);
    printf("[transpose] %-5s: mismatches=%ld  max_abs=%.3e  -> %s\n",
           name, mism, max_abs, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int runTransposeVerify(const char* xclbin_path) {
    const int N = 256;   // multiple of the tile size (32) -> 8x8 tiles

    std::mt19937 rng(5);
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
    std::vector<float> in((size_t)N * N);
    for (auto& v : in) v = uni(rng);

    std::vector<float> d_naive((size_t)N * N), d_tiled((size_t)N * N);

    printf("[transpose] verify: N=%d (both variants, one session)\n", N);
    runTranspose(in.data(), N, d_naive.data(), d_tiled.data(), xclbin_path);

    int rc = 0;
    rc |= check_bitexact("naive", d_naive, in, N);
    rc |= check_bitexact("tiled", d_tiled, in, N);
    return rc;
}

} // namespace plalgo
