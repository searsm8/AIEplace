// host.cpp -- XRT host for fft_pl: runs pl_algo's FFT-based DCT/IDCT/IDXST module
// (../pl/top.cpp -> vck5000/pl/src/pl_algo/src/modules/fft_pl.hpp) on real hardware and checks
// each transform against the SAME naive double-precision golden used by the offline tier-1 test
// (vck5000/test/fft_pl_test.cpp) -- this is that test's round-trip taken onto real silicon,
// not a new golden.
//
//   flow: fill in[] with random values -> run mode 0/1/2 (DCT/IDCT/IDXST) on the `fft_pl_top`
//         PL kernel -> read out[] back -> compare rel_rms against DCT/IDCT/IDXST_naive -> assert
//         < TOL for every transform, exit 0/non-zero.
//
//   usage: ./host <xclbin>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"
#include "experimental/xrt_system.h"

#ifndef PL_GRID
#define PL_GRID 64
#endif
static constexpr int N = PL_GRID;
static const double PI = 3.14159265358979323846;

// ---- naive double golden, identical to vck5000/test/fft_pl_test.cpp ----
static std::vector<double> DCT_naive(const std::vector<double>& in) {
    int n = in.size(); std::vector<double> r(n);
    for (int k = 0; k < n; k++) { double s = 0; for (int i = 0; i < n; i++) s += in[i]*std::cos(PI/n*(i+0.5)*k); r[k]=s; }
    return r;
}
static std::vector<double> IDCT_naive(const std::vector<double>& in) {
    int n = in.size(); std::vector<double> r(n);
    for (int k = 0; k < n; k++) { double s = 0; for (int i = 1; i < n; i++) s += in[i]*std::cos(PI/n*(k+0.5)*i); r[k]=0.5*in[0]+s; }
    return r;
}
static std::vector<double> IDXST_naive(const std::vector<double>& in) {
    int n = in.size(); std::vector<double> t(n); t[0]=in[0];
    for (int i=1;i<n;i++) t[i]=in[n-i];
    t=IDCT_naive(t);
    for (int i=1;i<n;i+=2) t[i]*=-1;
    return t;
}
static double rel_rms(const float* a, const std::vector<double>& b) {
    double e=0,n=0; for (size_t i=0;i<b.size();i++){double d=a[i]-b[i]; e+=d*d; n+=b[i]*b[i];} return std::sqrt(e/(n+1e-30));
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <xclbin>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* xclbin_path = argv[1];
    const size_t bytes = (size_t)N * sizeof(float);

    // ---- device / xclbin ----
    // The node can have multiple cards (e.g. U55C + VCK5000); probe each enumerated device
    // and use the first one that accepts this xclbin, rather than assuming index 0 is the
    // VCK5000 (see ../../add1_pl/src/host/host.cpp -- same fix, verified on real hardware).
    xrt::device device;
    xrt::uuid   uuid;
    bool        loaded = false;
    unsigned int ndev = xrt::system::enumerate_devices();
    for (unsigned int i = 0; i < ndev; i++) {
        try {
            xrt::device d = xrt::device(i);
            uuid = d.load_xclbin(xclbin_path);
            device = d;
            loaded = true;
            break;
        } catch (const std::exception&) {
            continue;
        }
    }
    if (!loaded) {
        std::printf("failed to load %s on any of %u device(s)\n", xclbin_path, ndev);
        return EXIT_FAILURE;
    }
    xrt::kernel k = xrt::kernel(device, uuid, "fft_pl_top");

    // ---- buffers: in (group 0), out (group 1) ----
    xrt::bo bo_in  = xrt::bo(device, bytes, k.group_id(0));
    xrt::bo bo_out = xrt::bo(device, bytes, k.group_id(1));
    float* in_map  = bo_in.map<float*>();
    float* out_map = bo_out.map<float*>();

    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> uni(0, 1);
    std::vector<double> xd(N);
    for (int i = 0; i < N; i++) { xd[i] = uni(rng); in_map[i] = (float)xd[i]; }
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // float radix-2 FFT vs double naive golden; fft_pl_test.cpp observes worst ~3.9e-07 (IDXST).
    const double TOL = 1e-6;
    static const char* names[3] = {"DCT", "IDCT", "IDXST"};
    double worst = 0.0;
    bool all_ok = true;

    for (int mode = 0; mode < 3; mode++) {
        std::memset(out_map, 0, bytes);
        bo_out.sync(XCL_BO_SYNC_BO_TO_DEVICE);

        xrt::run run = k(bo_in, bo_out, mode);
        run.wait();
        bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

        std::vector<double> golden = (mode == 0) ? DCT_naive(xd)
                                    : (mode == 1) ? IDCT_naive(xd)
                                                  : IDXST_naive(xd);
        double e = rel_rms(out_map, golden);
        bool ok = e < TOL;
        std::printf("%-5s rel_rms=%.3e  %s\n", names[mode], e, ok ? "PASS" : "FAIL");
        worst = std::fmax(worst, e);
        all_ok = all_ok && ok;
    }

    std::printf("%s  N=%d  (worst rel_rms=%.3e, tol %.0e)  fft_pl on VCK5000\n",
                all_ok ? "TEST PASSED" : "TEST FAILED", N, worst, TOL);
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
