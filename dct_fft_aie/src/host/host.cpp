// host.cpp -- drives the DCT-transpose <-> AIE-FFT integration test on the VCK5000.
//
//   flow:  fill mat_in (1024x1024 real) -> start AIE FFT graph (128 frames/lane) -> run the
//          PL `top` kernel (streams rows through the pool, writes the transposed transform)
//          -> read mat_out back -> assert rel_rms vs the double golden < TOL, exit 0/non-zero.
//
// The AIE FFT is single-precision cfloat, so the bar is a HARDWARE tolerance (~1e-3), not the
// ~1e-8 the software model hits. That is deliberate: a real integration failure -- a mis-wired
// lane, a dropped/duplicated beat, an endian/pack error, a graph-iteration mismatch -- shows up
// as an order-1 or structural error, not a few-percent drift. TOL catches those with margin.
//
//   usage: ./host <xclbin> [dct|idct|idxst]     (default dct)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <random>

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"
#include "experimental/xrt_graph.h"

#include "golden.hpp"

// Must match GRID in src/pl/formats.hpp and FFT_POINT_SIZE in src/aie/DensityFFTGraph.h.
static const int N          = 1024;
static const int FFT_LANES  = 8;
static const double TOL     = 1e-3;   // single-precision AIE FFT + PL float, whole-matrix rel_rms

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <xclbin> [dct|idct|idxst]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* xclbin_path = argv[1];
    std::string mode = (argc >= 3) ? argv[2] : "dct";
    int xform = golden::XF_DCT;
    if      (mode == "dct")   xform = golden::XF_DCT;
    else if (mode == "idct")  xform = golden::XF_IDCT;
    else if (mode == "idxst") xform = golden::XF_IDXST;
    else { std::printf("unknown transform '%s' (dct|idct|idxst)\n", mode.c_str()); return EXIT_FAILURE; }

    const size_t elems = (size_t)N * N;
    const size_t bytes = elems * sizeof(float);

    // ---- device / xclbin ----
    xrt::device device = xrt::device(0);
    xrt::uuid   uuid   = device.load_xclbin(xclbin_path);
    xrt::kernel top    = xrt::kernel(device, uuid, "top");
    xrt::graph  fft    = xrt::graph(device, uuid, "dct_fft_graph");  // instance name from graph.cpp

    // ---- buffers: mat_in (group 0), mat_out (group 1) ----
    xrt::bo bo_in  = xrt::bo(device, bytes, top.group_id(0));
    xrt::bo bo_out = xrt::bo(device, bytes, top.group_id(1));
    float* in_map  = bo_in.map<float*>();
    float* out_map = bo_out.map<float*>();

    // ---- reproducible test matrix, and the same values in the golden's Mat form ----
    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
    golden::Mat in_mat(N, golden::Vec(N));
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) {
            float v = uni(rng);
            in_map[(size_t)r * N + c] = v;
            in_mat[r][c] = (double)v;
        }
    std::memset(out_map, 0, bytes);
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // ---- run: start the AIE FFT graph (one frame/lane per row-batch), then the PL kernel ----
    // Each graph iteration = FFT_LANES instances x 1 frame = FFT_LANES rows; N rows total.
    fft.run(N / FFT_LANES);
    xrt::run run = top(bo_in, bo_out, xform);
    run.wait();
    fft.wait();

    bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    // ---- verify: device output vs double golden (makhoul recipe = what the PL/AIE implement) ----
    golden::Mat gold = golden::pass_golden(in_mat, xform, /*makhoul=*/true);
    golden::Mat dev(N, golden::Vec(N));
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            dev[r][c] = (double)out_map[(size_t)r * N + c];

    double rr = golden::rel_rms(dev, gold);
    if (rr < TOL) {
        std::printf("TEST PASSED  transform=%s  rel_rms=%.3e (tol %.1e)\n", mode.c_str(), rr, TOL);
        return EXIT_SUCCESS;
    }
    std::printf("TEST FAILED  transform=%s  rel_rms=%.3e (tol %.1e)\n", mode.c_str(), rr, TOL);
    // point at the first gross mismatch to aid debugging a wiring failure.
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) {
            double d = std::fabs(dev[r][c] - gold[r][c]);
            if (d > 1e-2 * (1.0 + std::fabs(gold[r][c]))) {
                std::printf("  first big diff at [%d][%d]: dev=%.5g gold=%.5g\n", r, c, dev[r][c], gold[r][c]);
                return EXIT_FAILURE;
            }
        }
    return EXIT_FAILURE;
}
