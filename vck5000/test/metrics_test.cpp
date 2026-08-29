// Verify metrics.hpp (metrics) -- the two scalars the host policy reads each iteration:
//   out[0] HPWL         = sum over unmasked nets of (max_x-min_x)+(max_y-min_y) of the net's pins
//   out[1] overflow_sum = sum over bins of max(0, rho - target_density)
// The module accumulates both in double (a float sum over ~1e6 nets/bins is order-dependent to
// ~0.3%, and the metric drives convergence, so it must be reproducible) then narrows to float.
//
// THREE ASSERTIONS:
//   [1] HPWL      -- module out[0] vs a double golden that reduces the same net-major pins by CSR
//                    net. Only the final float narrowing (~1e-7) separates them. Mirrors
//                    DataBase::computeTotalWirelength and hpwl_gradient's bbox pass.
//   [2] OVERFLOW  -- module out[1] vs a double golden sum of max(0, rho-td) over all bins.
//   [3] MASKING   -- perturbing a masked net's pins (net == -1, degree<=1 or >IGNORE_NET_DEGREE)
//                    must not move HPWL: those nets contribute 0. Bit-identical out[0].
//
// The design mirrors the packing metrics assumes: net-major pins carrying ABSOLUTE positions (P2),
// net == -1 on a masked net, contiguous per net per net_ptr.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB (guards formats.hpp's HLS headers)
#include "modules/metrics.hpp"            // the real module (uses N_BINS/GRID + NodePin)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace plalgo;                    // NodePin, N_BINS, metrics, IGNORE_NET_DEGREE

struct Design {
    int num_nets = 0;
    std::vector<int>     net_ptr;          // [num_nets+1] CSR
    std::vector<NodePin> pins;             // net-major, absolute positions, net==-1 if masked
    std::vector<int>     masked_pin_idx;   // indices into pins[] of masked nets
};

static Design build_design(unsigned seed) {
    Design d;
    d.num_nets = 5000;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(0.0f, 10000.0f);
    std::uniform_int_distribution<int>    deg_pick(2, 8);
    d.net_ptr.push_back(0);
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        int  deg;
        bool masked;
        if (net_id == 0 || net_id == d.num_nets - 1) { deg = 1;   masked = true; }  // ends masked
        else if (net_id % 400 == 3)  { deg = 140; masked = true; }                   // > IGNORE_NET_DEGREE
        else if (net_id % 400 == 7)  { deg = 1;   masked = true; }                   // degree <= 1
        else                         { deg = deg_pick(rng); masked = false; }
        const int beg = (int)d.pins.size();
        for (int k = 0; k < deg; k++) {
            NodePin r;
            r.node_idx = 0;                 // metrics never reads node_idx (positions are absolute)
            r.x = pos(rng); r.y = pos(rng);
            r.net = masked ? -1 : net_id;
            d.pins.push_back(r);
            if (masked) d.masked_pin_idx.push_back(beg + k);
        }
        d.net_ptr.push_back((int)d.pins.size());
    }
    return d;
}

static double golden_hpwl(const Design& d) {
    double total = 0.0;
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        const int beg = d.net_ptr[net_id], end = d.net_ptr[net_id + 1];
        if (beg == end || d.pins[beg].net < 0) continue;    // masked / empty
        float mxx = -1e30f, mnx = 1e30f, mxy = -1e30f, mny = 1e30f;
        for (int p = beg; p < end; p++) {
            const float x = d.pins[p].x, y = d.pins[p].y;
            mxx = std::max(mxx, x); mnx = std::min(mnx, x);
            mxy = std::max(mxy, y); mny = std::min(mny, y);
        }
        total += (double)((mxx - mnx) + (mxy - mny));
    }
    return total;
}

int main() {
    Design d = build_design(20260828u);

    const float target_density = 0.9f;
    std::mt19937 rng(1234u);
    std::uniform_real_distribution<float> rho_dist(0.0f, 1.6f);   // straddles td so overflow fires
    std::vector<float> rho(N_BINS);
    double ovfl_ref = 0.0;
    for (int b = 0; b < N_BINS; b++) {
        rho[b] = rho_dist(rng);
        const double excess = (double)rho[b] - (double)target_density;
        if (excess > 0.0) ovfl_ref += excess;
    }

    float out[2] = { -1.0f, -1.0f };
    metrics(d.net_ptr.data(), d.pins.data(), rho.data(), d.num_nets, target_density, out);

    const double hpwl_ref = golden_hpwl(d);
    const double hpwl_rel = std::fabs((double)out[0] - hpwl_ref) / hpwl_ref;
    const double ovfl_rel = std::fabs((double)out[1] - ovfl_ref) / ovfl_ref;

    bool ok = true;
    const double HPWL_TOL = 1e-6, OVFL_TOL = 1e-6;   // float narrowing of a double sum only
    if (!(hpwl_rel < HPWL_TOL)) { printf("FAIL [1] HPWL rel=%.3e (tol %.0e)\n", hpwl_rel, HPWL_TOL); ok = false; }
    if (!(ovfl_rel < OVFL_TOL)) { printf("FAIL [2] overflow rel=%.3e (tol %.0e)\n", ovfl_rel, OVFL_TOL); ok = false; }

    // ---- [3] MASKING: perturbing masked pins must not change HPWL ----
    Design pert = d;
    for (int p : pert.masked_pin_idx) { pert.pins[p].x += 5000.0f; pert.pins[p].y -= 5000.0f; }
    float out_p[2] = { -1.0f, -1.0f };
    metrics(pert.net_ptr.data(), pert.pins.data(), rho.data(), pert.num_nets, target_density, out_p);
    const bool mask_ok = (out_p[0] == out[0]);
    if (pert.masked_pin_idx.empty()) { printf("FAIL [3] no masked pins in design\n"); ok = false; }
    if (!mask_ok) { printf("FAIL [3] masked-pin perturb changed HPWL %.8e -> %.8e\n", out[0], out_p[0]); ok = false; }

    printf("[1] hpwl       out=%.8e golden=%.8e rel=%.3e (tol %.0e)\n", (double)out[0], hpwl_ref, hpwl_rel, HPWL_TOL);
    printf("[2] overflow   out=%.8e golden=%.8e rel=%.3e (tol %.0e)\n", (double)out[1], ovfl_ref, ovfl_rel, OVFL_TOL);
    printf("[3] masking    %d masked pins perturbed, HPWL %s\n", (int)pert.masked_pin_idx.size(), mask_ok ? "unchanged" : "CHANGED");
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
