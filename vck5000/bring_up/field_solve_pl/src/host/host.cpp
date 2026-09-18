// host.cpp -- XRT host for field_solve_pl: runs pl_algo's FULL electrostatic field solve
// (../pl/top.cpp -> vck5000/pl/src/pl_algo/src/modules/field_solve_pl.hpp) on real hardware and
// checks Ex/Ey against the SAME naive double-precision golden used by the offline tier-1 test
// (vck5000/test/field_solve_test.cpp) -- this is that test's round-trip taken onto real silicon,
// not a new golden.
//
//   flow: fill rho[N*N] with random values -> run the `field_solve_top` PL kernel (forward 2D
//         DCT -> spectral multiply -> inverse IDCT/IDXST, all on-chip) -> read Ex[N*N], Ey[N*N]
//         back -> compare rel_rms against the naive double solve -> assert < TOL, exit 0/non-zero.
//
//   usage: ./host <xclbin>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"

#ifndef PL_GRID
#define PL_GRID 64
#endif
static constexpr int N = PL_GRID;
static const double PI = 3.14159265358979323846;

// ---- naive double golden, identical to vck5000/test/field_solve_test.cpp ----
using V = std::vector<double>; using M = std::vector<V>;
static V DCTn(const V& in){int n=in.size();V r(n);for(int k=0;k<n;k++){double s=0;for(int i=0;i<n;i++)s+=in[i]*cos(PI/n*(i+0.5)*k);r[k]=s;}return r;}
static V IDCTn(const V& in){int n=in.size();V r(n);for(int k=0;k<n;k++){double s=0;for(int i=1;i<n;i++)s+=in[i]*cos(PI/n*(k+0.5)*i);r[k]=0.5*in[0]+s;}return r;}
static V IDXSTn(const V& in){int n=in.size();V t(n);t[0]=in[0];for(int i=1;i<n;i++)t[i]=in[n-i];t=IDCTn(t);for(int i=1;i<n;i+=2)t[i]*=-1;return t;}
static M tr(const M&A){int n=A.size();M T(n,V(n));for(int i=0;i<n;i++)for(int j=0;j<n;j++)T[i][j]=A[j][i];return T;}
static M rows(const M&A,V(*f)(const V&)){M R;for(auto&r:A)R.push_back(f(r));return R;}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <xclbin>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* xclbin_path = argv[1];
    const size_t bytes = (size_t)N * N * sizeof(float);

    // ---- device / xclbin ----
    // find the VCK5000 card (default bdf c1:00.1)
    const char* bdf_env = std::getenv("VCK5000_BDF"); // env var to override the default BDF
    const std::string bdf = bdf_env ? bdf_env : "c1:00.1"; // default VCK5000 BDF
    xrt::device device;
    try {
        device = xrt::device(bdf);
    } catch (const std::exception& e) {
        std::printf("failed to open device at BDF %s: %s\n", bdf.c_str(), e.what());
        return EXIT_FAILURE;
    }

    // load the xclbin and create the kernel object
    xrt::uuid uuid = device.load_xclbin(xclbin_path);
    xrt::kernel k = xrt::kernel(device, uuid, "field_solve_top");

    // ---- buffers: rho (group 0), Ex (group 1), Ey (group 2) ----
    xrt::bo bo_rho = xrt::bo(device, bytes, k.group_id(0));
    xrt::bo bo_ex  = xrt::bo(device, bytes, k.group_id(1));
    xrt::bo bo_ey  = xrt::bo(device, bytes, k.group_id(2));
    float* rho_map = bo_rho.map<float*>();
    float* ex_map  = bo_ex.map<float*>();
    float* ey_map  = bo_ey.map<float*>();

    // Same seed/distribution as field_solve_test.cpp so the numbers line up directly.
    std::mt19937 rng(777);
    std::uniform_real_distribution<double> uni(0, 1);
    M rho(N, V(N));
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) { rho[i][j] = uni(rng); rho_map[i * N + j] = (float)rho[i][j]; }
    std::memset(ex_map, 0, bytes);
    std::memset(ey_map, 0, bytes);
    bo_rho.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_ex.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_ey.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // ---- golden: forward 2D DCT -> spectral -> inverse (double) ----
    M auv = tr(rows(tr(rows(rho, DCTn)), DCTn));
    M ExG(N, V(N, 0)), EyG(N, V(N, 0));
    for (int u = 0; u < N; u++)
        for (int v = 0; v < N; v++) {
            if (u == 0 && v == 0) continue;
            double wu = 2 * PI * u / N, wv = 2 * PI * v / N, d = wu * wu + wv * wv;
            ExG[u][v] = auv[u][v] * wu / d;
            EyG[u][v] = auv[u][v] * wv / d;
        }
    ExG = rows(ExG, IDCTn);  EyG = rows(EyG, IDXSTn); ExG = tr(ExG); EyG = tr(EyG);
    ExG = rows(ExG, IDXSTn); EyG = rows(EyG, IDCTn);  ExG = tr(ExG); EyG = tr(EyG);

    // ---- run on hardware ----
    xrt::run run = k(bo_rho, bo_ex, bo_ey);
    run.wait();
    bo_ex.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    bo_ey.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    // ---- verify ----
    double ex_e = 0, ex_n = 0, ey_e = 0, ey_n = 0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            double dx = ex_map[i * N + j] - ExG[i][j]; ex_e += dx * dx; ex_n += ExG[i][j] * ExG[i][j];
            double dy = ey_map[i * N + j] - EyG[i][j]; ey_e += dy * dy; ey_n += EyG[i][j] * EyG[i][j];
        }
    double ex_rel = std::sqrt(ex_e / (ex_n + 1e-30));
    double ey_rel = std::sqrt(ey_e / (ey_n + 1e-30));
    std::printf("Ex rel_rms=%.3e  Ey rel_rms=%.3e\n", ex_rel, ey_rel);

    // float PL field solve vs double naive golden. field_solve_test.cpp observes ~9.8e-07 and
    // sets its bound at 2e-6 (too tight at 1e-6 to be stable); match that here.
    const double TOL = 2e-6;
    double worst = std::fmax(ex_rel, ey_rel);
    bool ok = worst < TOL;
    std::printf("%s  N=%d  (worst rel_rms=%.3e, tol %.0e)  field_solve_pl on VCK5000\n",
                ok ? "TEST PASSED" : "TEST FAILED", N, worst, TOL);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
