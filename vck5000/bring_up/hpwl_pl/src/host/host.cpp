// host.cpp -- XRT host for hpwl_pl: runs pl_algo's HPWL gradient compute unit
// (../pl/top.cpp -> vck5000/pl/src/pl_algo/src/modules/hpwl_gradient.hpp: plalgo::hpwl_CU)
// on real hardware and checks node_grad against the SAME exact-exp full-WA golden used by
// the pl_algo sw_emu bring-up check (vck5000/host/src/pl_algo/src/HpwlGradVerify.cpp
// gradientGolden()) -- this is that check's math taken onto real silicon, not a new golden.
// hpwl_CU itself approximates exp() with a host-supplied LUT (see hpwl_gradient.hpp), so the
// tolerance (2e-2 relative RMS) isolates LUT-vs-exp error, same as HpwlGradVerify.
//
//   flow: build a small synthetic random netlist (no parser -- movable + fixed nodes, nets
//         of random degree, a few degree-1 "no gradient" nets to exercise the net<0 skip
//         path) -> upload node_pos/net_ptr/pins/npins/exp_lut -> run the `hpwl_top` PL kernel
//         (three segmented reductions, all on-chip register accumulation, DDR-resident
//         arrays) -> read node_grad[num_movable] back -> compare rel_rms against the exact
//         double-precision-free (float, matches the kernel) exp() golden -> assert < TOL,
//         exit 0/non-zero.
//
//   usage: ./host <xclbin>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <numeric>

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"

#include "host_interface.hpp"

using plalgo::coord_t;
using plalgo::NodePin;
using plalgo::NetBBox;
using plalgo::NetSums;

// ---- synthetic design size (arbitrary -- this module has no size cap, unlike
// field_solve_pl's on-chip fixed grid). Defaults match the original bring-up size; override
// for a scaling sweep via CLI: ./host <xclbin> [movable] [fixed] [num_nets] [timing_iters] ----
static int M          = 800;   // movable nodes
static int NUM_FIXED  = 200;   // fixed nodes (FIXED components + IOPads, in v0 terms)
static int N          = M + NUM_FIXED;
static int NUM_NETS   = 550;   // includes ~1/11 degree-1 "no gradient" nets

// Normalized exp LUT, matching the kernel's lookup and HpwlGradVerify.cpp exactly:
// lut[i] = exp(-i*STEP_NORM), index = d * inv_lut_step where
// inv_lut_step = 1/(STEP_NORM*gamma), so lut(d) ~= exp(-d/gamma).
static constexpr float STEP_NORM  = 0.05f;
static constexpr int   GAMMA_MULT = 12;

static std::vector<float> buildExpLut(int& lut_size) {
    lut_size = (int)(GAMMA_MULT / STEP_NORM) + 2;
    std::vector<float> lut(lut_size);
    for (int i = 0; i < lut_size; i++) lut[i] = std::exp(-(float)i * STEP_NORM);
    return lut;
}

// Exact-exp full-WA gradient golden -- identical math to
// vck5000/host/src/pl_algo/src/HpwlGradVerify.cpp gradientGolden(). Float throughout
// (matches the kernel's precision) so the comparison isolates LUT-vs-exp error only.
static std::vector<coord_t> gradientGolden(const std::vector<coord_t>& node_pos,
                                           const std::vector<int32_t>& net_ptr,
                                           const std::vector<NodePin>& pins,
                                           float inv_gamma) {
    std::vector<coord_t> grad(M, coord_t{0.0f, 0.0f});

    for (int n = 0; n < NUM_NETS; n++) {
        const int beg = net_ptr[n], end = net_ptr[n + 1];
        const int deg = end - beg;
        if (deg <= 1) continue;

        float maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
        for (int p = beg; p < end; p++) {
            const NodePin& r = pins[p];
            const float x = node_pos[r.node_idx].x + r.off_x;
            const float y = node_pos[r.node_idx].y + r.off_y;
            maxx = std::max(maxx, x); minx = std::min(minx, x);
            maxy = std::max(maxy, y); miny = std::min(miny, y);
        }

        float Bpx = 0, Bmx = 0, Cpx = 0, Cmx = 0, Bpy = 0, Bmy = 0, Cpy = 0, Cmy = 0;
        for (int p = beg; p < end; p++) {
            const NodePin& r = pins[p];
            const float x = node_pos[r.node_idx].x + r.off_x;
            const float y = node_pos[r.node_idx].y + r.off_y;
            const float apx = std::exp((x - maxx) * inv_gamma);
            const float amx = std::exp((minx - x) * inv_gamma);
            const float apy = std::exp((y - maxy) * inv_gamma);
            const float amy = std::exp((miny - y) * inv_gamma);
            Bpx += apx; Bmx += amx; Cpx += apx * x; Cmx += amx * x;
            Bpy += apy; Bmy += amy; Cpy += apy * y; Cmy += amy * y;
        }
        const float bpx2 = 1.0f / (Bpx * Bpx), bmx2 = 1.0f / (Bmx * Bmx);
        const float bpy2 = 1.0f / (Bpy * Bpy), bmy2 = 1.0f / (Bmy * Bmy);

        for (int p = beg; p < end; p++) {
            const NodePin& r = pins[p];
            if (r.node_idx >= M) continue;  // fixed: no stored gradient
            const float x = node_pos[r.node_idx].x + r.off_x;
            const float y = node_pos[r.node_idx].y + r.off_y;
            const float apx = std::exp((x - maxx) * inv_gamma);
            const float amx = std::exp((minx - x) * inv_gamma);
            const float apy = std::exp((y - maxy) * inv_gamma);
            const float amy = std::exp((miny - y) * inv_gamma);
            const float px = ((1.0f + x * inv_gamma) * Bpx - Cpx * inv_gamma) * (apx * bpx2)
                           - ((1.0f - x * inv_gamma) * Bmx + Cmx * inv_gamma) * (amx * bmx2);
            const float py = ((1.0f + y * inv_gamma) * Bpy - Cpy * inv_gamma) * (apy * bpy2)
                           - ((1.0f - y * inv_gamma) * Bmy + Cmy * inv_gamma) * (amy * bmy2);
            grad[r.node_idx].x += px;
            grad[r.node_idx].y += py;
        }
    }
    return grad;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <xclbin> [movable_nodes] [fixed_nodes] [num_nets] [timing_iters]\n",
                    argv[0]);
        return EXIT_FAILURE;
    }
    const char* xclbin_path = argv[1];
    int TIMING_ITERS = 200;
    if (argc >= 3) M           = std::atoi(argv[2]);
    if (argc >= 4) NUM_FIXED   = std::atoi(argv[3]);
    if (argc >= 5) NUM_NETS    = std::atoi(argv[4]);
    if (argc >= 6) TIMING_ITERS = std::atoi(argv[5]);
    N = M + NUM_FIXED;

    // ---- synthetic design: random positions, random-degree nets, ~1/11 masked (degree-1,
    // net=-1) nets interspersed to exercise the kernel's net<0 skip path ----
    std::mt19937 rng(777);
    std::uniform_real_distribution<float> pos_uni(0.0f, 1000.0f);
    std::uniform_real_distribution<float> off_uni(-3.0f, 3.0f);
    std::uniform_real_distribution<float> off_prob(0.0f, 1.0f);
    std::uniform_int_distribution<int>    node_uni(0, N - 1);
    std::uniform_int_distribution<int>    deg_uni(2, 8);

    std::vector<coord_t> node_pos(N);
    for (int i = 0; i < N; i++) { node_pos[i].x = pos_uni(rng); node_pos[i].y = pos_uni(rng); }

    std::vector<int32_t> net_ptr(NUM_NETS + 1, 0);
    std::vector<NodePin> pins;
    pins.reserve((size_t)NUM_NETS * 5);
    for (int net = 0; net < NUM_NETS; net++) {
        const bool masked = (net % 11 == 0);
        const int  deg    = masked ? 1 : deg_uni(rng);
        net_ptr[net] = (int32_t)pins.size();
        for (int k = 0; k < deg; k++) {
            NodePin r;
            r.node_idx = node_uni(rng);
            const bool has_offset = off_prob(rng) < 0.15f;
            r.off_x = has_offset ? off_uni(rng) : 0.0f;
            r.off_y = has_offset ? off_uni(rng) : 0.0f;
            r.net   = masked ? -1 : net;
            pins.push_back(r);
        }
    }
    net_ptr[NUM_NETS] = (int32_t)pins.size();
    const int num_pins = (int)pins.size();

    // Node-major pin stream (pass 3): movable, gradient-bearing pins, sorted ascending by
    // node so each node's pins are contiguous -- mirrors pl_algo Packer.cpp exactly.
    std::vector<NodePin> npins;
    npins.reserve(pins.size());
    for (const NodePin& r : pins)
        if (r.net >= 0 && r.node_idx < M) npins.push_back(r);
    std::stable_sort(npins.begin(), npins.end(),
                     [](const NodePin& a, const NodePin& b){ return a.node_idx < b.node_idx; });
    const int num_npins = (int)npins.size();

    // gamma from the design's coordinate span (1% of the larger extent), same rule
    // HpwlGradVerify.cpp uses on real designs.
    float maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
    for (const coord_t& c : node_pos) {
        maxx = std::max(maxx, c.x); minx = std::min(minx, c.x);
        maxy = std::max(maxy, c.y); miny = std::min(miny, c.y);
    }
    const float span         = std::max(maxx - minx, maxy - miny);
    const float gamma        = 0.01f * span;
    const float inv_gamma    = 1.0f / gamma;
    const float inv_lut_step = 1.0f / (STEP_NORM * gamma);

    int lut_size;
    std::vector<float> lut = buildExpLut(lut_size);

    std::printf("[hpwl_pl] M=%d N=%d nets=%d pins=%d npins=%d  span=%.3g gamma=%.3g lut=%d\n",
               M, N, NUM_NETS, num_pins, num_npins, span, gamma, lut_size);

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
    xrt::kernel k = xrt::kernel(device, uuid, "hpwl_top");

    // ---- buffers: node_pos(0) net_ptr(1) pins(2) npins(3) exp_lut(4) bb(5) sums(6)
    // node_grad(7) -- bb/sums are device-only scratch, host allocates but never touches ----
    const size_t node_bytes  = node_pos.size() * sizeof(coord_t);
    const size_t nptr_bytes  = net_ptr.size()  * sizeof(int32_t);
    const size_t pins_bytes  = pins.size()     * sizeof(NodePin);
    const size_t npins_bytes = npins.size()    * sizeof(NodePin);
    const size_t lut_bytes   = (size_t)lut_size * sizeof(float);
    const size_t bb_bytes    = (size_t)NUM_NETS * sizeof(NetBBox);
    const size_t sums_bytes  = (size_t)NUM_NETS * sizeof(NetSums);
    const size_t grad_bytes  = (size_t)M         * sizeof(coord_t);

    xrt::bo bo_node  = xrt::bo(device, node_bytes,  k.group_id(0));
    xrt::bo bo_nptr  = xrt::bo(device, nptr_bytes,  k.group_id(1));
    xrt::bo bo_pins  = xrt::bo(device, pins_bytes,  k.group_id(2));
    xrt::bo bo_npins = xrt::bo(device, npins_bytes, k.group_id(3));
    xrt::bo bo_lut   = xrt::bo(device, lut_bytes,   k.group_id(4));
    xrt::bo bo_bb    = xrt::bo(device, bb_bytes,    k.group_id(5));
    xrt::bo bo_sums  = xrt::bo(device, sums_bytes,  k.group_id(6));
    xrt::bo bo_grad  = xrt::bo(device, grad_bytes,  k.group_id(7));

    std::memcpy(bo_node.map<void*>(),  node_pos.data(), node_bytes);
    std::memcpy(bo_nptr.map<void*>(),  net_ptr.data(),  nptr_bytes);
    std::memcpy(bo_pins.map<void*>(),  pins.data(),     pins_bytes);
    std::memcpy(bo_npins.map<void*>(), npins.data(),    npins_bytes);
    std::memcpy(bo_lut.map<void*>(),   lut.data(),      lut_bytes);
    std::memset(bo_grad.map<void*>(), 0, grad_bytes);

    bo_node.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_nptr.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_pins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_npins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_lut.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_grad.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // ---- golden: exact exp() full-WA gradient (float, same precision as the kernel) ----
    std::vector<coord_t> gold = gradientGolden(node_pos, net_ptr, pins, inv_gamma);

    // ---- run on hardware ----
    xrt::run run = k(bo_node, bo_nptr, bo_pins, bo_npins, bo_lut, bo_bb, bo_sums, bo_grad,
                     inv_gamma, inv_lut_step, lut_size, NUM_NETS, M, num_npins);
    run.wait();
    bo_grad.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    coord_t* grad_map = bo_grad.map<coord_t*>();

    // ---- verify (same metric/tolerance as HpwlGradVerify.cpp: LUT-vs-exp isolation) ----
    double max_abs = 0, max_rel = 0, sse = 0, sse_ref = 0;
    int counted = 0;
    for (int i = 0; i < M; i++) {
        const double ex = gold[i].x, ey = gold[i].y, ax = grad_map[i].x, ay = grad_map[i].y;
        const double dmag = std::sqrt((ax - ex) * (ax - ex) + (ay - ey) * (ay - ey));
        const double rmag = std::sqrt(ex * ex + ey * ey);
        max_abs = std::max(max_abs, dmag);
        sse += dmag * dmag; sse_ref += rmag * rmag;
        if (rmag > 1e-4) { max_rel = std::max(max_rel, dmag / rmag); counted++; }
    }
    const double rel_rms = (sse_ref > 0) ? std::sqrt(sse / sse_ref) : 0.0;  // ||err||/||ref||
    const double TOL     = 2e-2;  // LUT approximation, not a kernel-precision bound
    const bool   ok      = rel_rms < TOL;

    std::printf("max_abs=%.3e  max_rel=%.3e (%d nodes)  rel_rms=%.3e\n",
               max_abs, max_rel, counted, rel_rms);
    std::printf("%s  M=%d  (rel_rms=%.3e, tol %.0e)  hpwl_pl on VCK5000\n",
               ok ? "TEST PASSED" : "TEST FAILED", M, rel_rms, TOL);

    // ---- rough host-side wall-clock kernel timing ----
    // No on-chip instrumentation in this binary (see build/hw/HANDOFF.md path B for the
    // --profile_kernel on-chip-counter comparison); this is the zero-rebuild host-side number.
    // Re-launches the same already-verified inputs so no re-sync/re-verify is needed, and times
    // only run.wait() -- DMA setup is excluded, AXI-lite launch/scheduling overhead is included.
    // Sample count large enough to resolve p99 (not just repeat the max); TIMING_ITERS is a
    // CLI override (see usage) so a scaling sweep can shrink it for large netlists.
    std::vector<double> us_samples;
    us_samples.reserve(TIMING_ITERS);
    for (int it = 0; it < TIMING_ITERS; it++) {
        const auto t0 = std::chrono::steady_clock::now();
        xrt::run r = k(bo_node, bo_nptr, bo_pins, bo_npins, bo_lut, bo_bb, bo_sums, bo_grad,
                       inv_gamma, inv_lut_step, lut_size, NUM_NETS, M, num_npins);
        r.wait();
        const auto t1 = std::chrono::steady_clock::now();
        us_samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    std::vector<double> sorted_us = us_samples;
    std::sort(sorted_us.begin(), sorted_us.end());
    auto percentile = [&](double p) {
        const size_t idx = (size_t)(p * (double)(sorted_us.size() - 1));
        return sorted_us[idx];
    };
    const double us_min    = sorted_us.front();
    const double us_p50    = percentile(0.50);
    const double us_p90    = percentile(0.90);
    const double us_p95    = percentile(0.95);
    const double us_p99    = percentile(0.99);
    const double us_max    = sorted_us.back();
    const double us_mean   = std::accumulate(us_samples.begin(), us_samples.end(), 0.0)
                            / (double)us_samples.size();
    double us_var = 0.0;
    for (double v : us_samples) us_var += (v - us_mean) * (v - us_mean);
    us_var /= (double)us_samples.size();
    const double us_stddev = std::sqrt(us_var);

    std::printf("[hpwl_pl] kernel wall time over %d launches"
               " (host chrono around run.wait(), DMA excluded, launch overhead included):\n"
               "  min=%.1f  p50=%.1f  p90=%.1f  p95=%.1f  p99=%.1f  max=%.1f  us\n"
               "  mean=%.1f us  stddev=%.1f us\n"
               "  @ p50: %.3f Mnets/s  %.3f Mmovable-nodes/s  %.3f Mpins/s\n",
               TIMING_ITERS, us_min, us_p50, us_p90, us_p95, us_p99, us_max,
               us_mean, us_stddev,
               NUM_NETS / us_p50, M / us_p50, num_pins / us_p50);

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
