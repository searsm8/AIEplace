// host.cpp -- native XRT host for the Dhar HPWL-gradient kernel.
//
// Builds a small synthetic netlist (movable + fixed nodes, masked nets, and LIVE >16-pin nets so
// the cap has something to drop), folds the current probe into the pins on the CPU
// (refresh_net_pins/refresh_node_pins), computes the capped WA-HPWL gradient golden in double,
// then runs the kernel and checks the returned per-node gradient (rel_rms) and total HPWL. Same
// golden as test/hpwl_dhar_test.cpp -- this is that test carried across the real host<->PL path.
//
// Reuses the module header for the POD types, the refresh passes, DHAR_MAX_NET_DEGREE and the exp
// LUT geometry; the HLS pragmas inside are ignored under g++.

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <random>
#include <cmath>

#include "modules/hpwl_gradient_dhar.hpp"

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"
#include "experimental/xrt_system.h"

using namespace plalgo;

// LUT geometry, matching host/src/pl_algo/src/main.cpp:244 and the tier-1 harness.
static constexpr float PLACE_STEP_NORM = 0.05f;
static constexpr int   GAMMA_MULT      = 12;

struct Design {
    int M = 0, N = 0, num_nets = 0;
    std::vector<coord_t>   node_pos;
    std::vector<int>       net_ptr;
    std::vector<NodePin>   pins;
    std::vector<NodePin>   node_pins;
    std::vector<int>       pin_to_npin;
    std::vector<PinOffset> pin_off;
    std::vector<PinOffset> node_pin_off;
    std::vector<int>       oversized_pin_idx;   // pins on LIVE >16-pin nets (cap coverage)
};

// Compact analogue of hpwl_dhar_test's build_design: kept small so it runs under sw_emu, but still
// straddles every branch -- masked (deg<=1 / deg>IGNORE), a deg exactly on the 16 cap, and a class
// of LIVE >16-pin nets the module must drop.
static Design build_design(int num_nets, unsigned seed) {
    Design d;
    d.num_nets = num_nets;
    d.M = num_nets - num_nets / 6;      // ~17% fixed
    d.N = d.M + num_nets / 6;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(0.0f, 10000.0f);
    std::uniform_int_distribution<int>    node_pick(0, d.N - 1);
    std::uniform_int_distribution<int>    deg_pick(2, 16);
    std::uniform_int_distribution<int>    coin(0, 9);
    std::uniform_real_distribution<float> off(-25.0f, 25.0f);

    d.node_pos.resize(d.N);
    for (int n = 0; n < d.N; n++) { d.node_pos[n].x = pos(rng); d.node_pos[n].y = pos(rng); }

    d.net_ptr.push_back(0);
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        int  deg;
        bool oversized = false;
        if (net_id == 0 || net_id == d.num_nets - 1) deg = 1;         // masked at both ends
        else if (net_id % 40 == 3)  deg = 140;                        // > IGNORE_NET_DEGREE (masked)
        else if (net_id % 40 == 7)  deg = 1;                          // degree <= 1 (masked)
        else if (net_id % 40 == 11) { deg = 24; oversized = true; }   // LIVE, > 16-pin cap
        else if (net_id % 40 == 17) deg = 16;                         // exactly on the cap
        else                        deg = deg_pick(rng);

        const int beg = (int)d.pins.size();
        for (int k = 0; k < deg; k++) {
            NodePin r;
            r.node_idx = node_pick(rng);
            r.x = r.y = 0.0f;                       // filled by refresh
            r.net = net_id;
            d.pins.push_back(r);
            PinOffset o;
            o.off_x = (coin(rng) == 0) ? off(rng) : 0.0f;
            o.off_y = (coin(rng) == 0) ? off(rng) : 0.0f;
            d.pin_off.push_back(o);
        }
        if (deg <= 1 || deg > IGNORE_NET_DEGREE)         // XPlace net_mask (host-side)
            for (int p = beg; p < (int)d.pins.size(); p++) d.pins[p].net = -1;
        else if (oversized)
            for (int p = beg; p < (int)d.pins.size(); p++) d.oversized_pin_idx.push_back(p);
        d.net_ptr.push_back((int)d.pins.size());
    }

    // Node-major stream: movable, gradient-bearing pins, stable-sorted by node (Packer.cpp).
    std::vector<int> order;
    for (int p = 0; p < (int)d.pins.size(); p++)
        if (d.pins[p].net >= 0 && d.pins[p].node_idx < d.M) order.push_back(p);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return d.pins[a].node_idx < d.pins[b].node_idx; });
    for (int p : order) { d.node_pins.push_back(d.pins[p]); d.node_pin_off.push_back(d.pin_off[p]); }

    d.pin_to_npin.assign(d.pins.size(), -1);
    for (int i = 0; i < (int)order.size(); i++) d.pin_to_npin[order[i]] = i;
    return d;
}

// Capped golden (double, LUT): identical to test/hpwl_dhar_test.cpp golden with use_lut=true.
static void golden(const Design& d, float inv_gamma, const std::vector<float>& lut,
                   int lut_size, float inv_lut_step,
                   std::vector<double>& gx, std::vector<double>& gy) {
    gx.assign(d.M, 0.0); gy.assign(d.M, 0.0);
    const double ig = (double)inv_gamma;
    auto E = [&](float dist) -> double {
        float idx_f = dist * inv_lut_step;
        int   idx   = (int)idx_f;
        if (idx >= lut_size - 1) return 0.0;
        double frac = (double)(idx_f - (float)idx);
        return (double)lut[idx] * (1.0 - frac) + (double)lut[idx + 1] * frac;
    };
    std::vector<double> Apx, Amx, Apy, Amy;
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        const int beg = d.net_ptr[net_id], end = d.net_ptr[net_id + 1];
        if (beg == end || d.pins[beg].net < 0) continue;
        const int deg = end - beg;
        if (deg > DHAR_MAX_NET_DEGREE) continue;               // Dhar cap
        std::vector<float> px(deg), py(deg);
        float mxx = -1e30f, mnx = 1e30f, mxy = -1e30f, mny = 1e30f;
        for (int k = 0; k < deg; k++) {
            px[k] = d.pins[beg + k].x; py[k] = d.pins[beg + k].y;
            mxx = std::max(mxx, px[k]); mnx = std::min(mnx, px[k]);
            mxy = std::max(mxy, py[k]); mny = std::min(mny, py[k]);
        }
        Apx.assign(deg, 0); Amx.assign(deg, 0); Apy.assign(deg, 0); Amy.assign(deg, 0);
        double Bpx = 0, Bmx = 0, Bpy = 0, Bmy = 0, Cpx = 0, Cmx = 0, Cpy = 0, Cmy = 0;
        for (int k = 0; k < deg; k++) {
            Apx[k] = E(mxx - px[k]); Amx[k] = E(px[k] - mnx);
            Apy[k] = E(mxy - py[k]); Amy[k] = E(py[k] - mny);
            Bpx += Apx[k]; Bmx += Amx[k]; Bpy += Apy[k]; Bmy += Amy[k];
            Cpx += Apx[k] * (double)px[k]; Cmx += Amx[k] * (double)px[k];
            Cpy += Apy[k] * (double)py[k]; Cmy += Amy[k] * (double)py[k];
        }
        const double ipx = 1.0 / (Bpx * Bpx), imx = 1.0 / (Bmx * Bmx);
        const double ipy = 1.0 / (Bpy * Bpy), imy = 1.0 / (Bmy * Bmy);
        for (int k = 0; k < deg; k++) {
            const int nd = d.pins[beg + k].node_idx;
            if (nd >= d.M) continue;
            const double x = px[k], y = py[k];
            gx[nd] += ((1.0 + x * ig) * Bpx - Cpx * ig) * (Apx[k] * ipx)
                    - ((1.0 - x * ig) * Bmx + Cmx * ig) * (Amx[k] * imx);
            gy[nd] += ((1.0 + y * ig) * Bpy - Cpy * ig) * (Apy[k] * ipy)
                    - ((1.0 - y * ig) * Bmy + Cmy * ig) * (Amy[k] * imy);
        }
    }
}

static double golden_hpwl(const Design& d) {
    double total = 0.0;
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        const int beg = d.net_ptr[net_id], end = d.net_ptr[net_id + 1];
        if (beg == end || d.pins[beg].net < 0) continue;
        if (end - beg > DHAR_MAX_NET_DEGREE) continue;
        float mxx = -1e30f, mnx = 1e30f, mxy = -1e30f, mny = 1e30f;
        for (int p = beg; p < end; p++) {
            mxx = std::max(mxx, d.pins[p].x); mnx = std::min(mnx, d.pins[p].x);
            mxy = std::max(mxy, d.pins[p].y); mny = std::min(mny, d.pins[p].y);
        }
        total += (double)((mxx - mnx) + (mxy - mny));
    }
    return total;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: %s <hpwl_gradient_dhar.xclbin> [num_nets]\n", argv[0]);
                    return EXIT_FAILURE; }
    const char* xclbin_path = argv[1];
    const int   num_nets    = (argc >= 3) ? atoi(argv[2]) : 600;

    Design d = build_design(num_nets, 20260909u);

    // exp LUT + gamma-dependent scalars (mid-run gamma), matching the tier-1 harness.
    const int lut_size = (int)(GAMMA_MULT / PLACE_STEP_NORM) + 2;
    std::vector<float> lut(lut_size);
    for (int i = 0; i < lut_size; i++) lut[i] = std::exp(-(float)i * PLACE_STEP_NORM);
    const float gamma        = 120.0f;
    const float inv_gamma    = 1.0f / gamma;
    const float inv_lut_step = 1.0f / (PLACE_STEP_NORM * gamma);

    // Fold the probe into the pins on the CPU, exactly as MODE_REFRESH_PINS would on device.
    refresh_net_pins(d.node_pos.data(), d.pin_off.data(), d.pins.data(), (int)d.pins.size());
    refresh_node_pins(d.node_pos.data(), d.node_pin_off.data(), d.node_pins.data(),
                      (int)d.node_pins.size());

    const int num_pins      = (int)d.pins.size();
    const int num_node_pins = (int)d.node_pins.size();
    printf("[info] design: %d movable / %d nodes, %d nets, %d pins, %d node_pins, "
           "%d oversized(>16) pins, lut %d\n",
           d.M, d.N, d.num_nets, num_pins, num_node_pins,
           (int)d.oversized_pin_idx.size(), lut_size);

    // ---- device / xclbin ----
    // The node can have multiple cards (e.g. U55C + VCK5000); probe each enumerated device
    // and use the first one that accepts this xclbin (see ../hpwl_pl, fft_pl,
    // field_solve_pl and add1_pl -- same fix, verified on real hardware). Expect an XRT
    // "err = -22" line per non-VCK5000 card probed before the load lands; that is benign
    // wrong-device noise, not a failure.
    xrt::device device;
    xrt::uuid   uuid;
    bool         loaded = false;
    unsigned int ndev   = xrt::system::enumerate_devices();
    for (unsigned int i = 0; i < ndev; i++) {
        try {
            xrt::device d_probe = xrt::device(i);
            uuid = d_probe.load_xclbin(xclbin_path);
            device = d_probe;
            loaded = true;
            break;
        } catch (const std::exception&) {
            continue;
        }
    }
    if (!loaded) {
        printf("failed to load %s on any of %u device(s)\n", xclbin_path, ndev);
        return EXIT_FAILURE;
    }
    xrt::kernel krnl   = xrt::kernel(device, uuid, "hpwl_gradient_dhar_top");

    // ---- allocate buffers in the kernel's memory banks (arg order = group_id) ----
    xrt::bo bo_net_ptr  = xrt::bo(device, (num_nets + 1) * sizeof(int),   krnl.group_id(0));
    xrt::bo bo_net_pins = xrt::bo(device, num_pins * sizeof(NodePin),     krnl.group_id(1));
    xrt::bo bo_npins    = xrt::bo(device, num_node_pins * sizeof(NodePin),krnl.group_id(2));
    xrt::bo bo_p2n      = xrt::bo(device, num_pins * sizeof(int),         krnl.group_id(3));
    xrt::bo bo_lut      = xrt::bo(device, lut_size * sizeof(float),       krnl.group_id(4));
    xrt::bo bo_pin_grad = xrt::bo(device, num_node_pins * sizeof(coord_t),krnl.group_id(5));
    xrt::bo bo_grad     = xrt::bo(device, d.M * sizeof(coord_t),          krnl.group_id(6));
    xrt::bo bo_hpwl     = xrt::bo(device, sizeof(float),                  krnl.group_id(7));

    std::copy(d.net_ptr.begin(),   d.net_ptr.end(),   bo_net_ptr.map<int*>());
    std::copy(d.pins.begin(),      d.pins.end(),      bo_net_pins.map<NodePin*>());
    std::copy(d.node_pins.begin(), d.node_pins.end(), bo_npins.map<NodePin*>());
    std::copy(d.pin_to_npin.begin(),d.pin_to_npin.end(),bo_p2n.map<int*>());
    std::copy(lut.begin(),         lut.end(),         bo_lut.map<float*>());

    bo_net_ptr.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_net_pins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_npins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_p2n.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_lut.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // ---- run ----
    xrt::run run = krnl(bo_net_ptr, bo_net_pins, bo_npins, bo_p2n, bo_lut, bo_pin_grad,
                        bo_grad, bo_hpwl, inv_gamma, inv_lut_step, lut_size,
                        num_nets, d.M, num_node_pins);
    run.wait();

    bo_grad.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    bo_hpwl.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    const coord_t* grad         = bo_grad.map<coord_t*>();
    const float    hpwl_emitted = *bo_hpwl.map<float*>();

    // ---- verify against the capped golden ----
    std::vector<double> gx, gy;
    golden(d, inv_gamma, lut, lut_size, inv_lut_step, gx, gy);

    double se = 0, sr = 0, worst = 0;
    for (int n = 0; n < d.M; n++) {
        const double ex = (double)grad[n].x - gx[n], ey = (double)grad[n].y - gy[n];
        se += ex * ex + ey * ey;
        sr += gx[n] * gx[n] + gy[n] * gy[n];
        worst = std::max(worst, std::max(std::fabs(ex), std::fabs(ey)));
    }
    const double rms_ref = std::sqrt(sr / (2.0 * d.M));
    const double rel_rms = std::sqrt(se / (2.0 * d.M)) / rms_ref;
    const double max_rel = worst / rms_ref;

    const double hpwl_ref = golden_hpwl(d);
    const double hpwl_rel = std::fabs((double)hpwl_emitted - hpwl_ref) / hpwl_ref;

    // Bounds match the tier-1 harness [1]/[6]; the LUT is identical on both sides, so only float
    // rounding through the tree remains.
    const double RMS_TOL = 1e-5, MAX_TOL = 1e-4, HPWL_TOL = 1e-6;
    const bool ok = rel_rms < RMS_TOL && max_rel < MAX_TOL && hpwl_rel < HPWL_TOL;

    printf("gradient  rel_rms=%.3e  max_rel=%.3e   (tol %.0e / %.0e)\n",
           rel_rms, max_rel, RMS_TOL, MAX_TOL);
    printf("hpwl      emitted=%.8e  golden=%.8e  rel=%.3e (tol %.0e)\n",
           (double)hpwl_emitted, hpwl_ref, hpwl_rel, HPWL_TOL);
    printf("%s\n", ok ? "TEST PASSED" : "TEST FAILED");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
