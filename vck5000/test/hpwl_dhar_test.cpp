// Verify hpwl_gradient_dhar.hpp (hpwl_gradient_dhar) against the sw_only WA-HPWL gradient golden.
//
// Same golden as hpwl_grad_test.cpp -- computeHpwlPartials_CPU (Partials.cpp:200), re-derived here
// in double precision -- but with Dhar's HARD CAP: nets with degree > DHAR_MAX_NET_DEGREE (16) are
// IGNORED, contributing to neither the gradient nor the HPWL. The golden below applies exactly the
// same cap, so if the module wrongly included (or wrongly dropped) a net, assertion [1] fails.
//
// SEVEN ASSERTIONS. Six mirror hpwl_grad_test; [CAP] is new and is the headline check for this
// module -- that nets over 16 pins are dropped:
//   [1] STRUCTURE   -- module (float, adder tree, LUT) vs golden (double, same LUT). Isolates the
//                      per-net term-gen -> adder tree -> combiner datapath, the bbox, the
//                      net-major -> node-major scatter, the WA quotient rule, masking and the cap.
//                      Only float rounding (now through a depth-4 tree, not a stream) is left.
//   [2] LUT BUDGET  -- module (float, LUT) vs golden (double, true exp()). What the exp LUT costs.
//   [3] ZEROING     -- movable nodes with no gradient-bearing pin must come back EXACTLY {0,0}.
//   [4] MASKING     -- perturbing a masked net's pins (net==-1) must not move any node gradient.
//   [CAP] 16-CAP    -- perturbing a LIVE >16-pin net's pins must not move any node gradient. This
//                      is [4] for the degree cap: those pins ARE in node_pins (the host packs them
//                      under IGNORE_NET_DEGREE=100), so the module must scatter 0 for them (phase Z
//                      + the deg>16 skip), or their node's reduction would pick up garbage.
//   [6] HPWL        -- the capped HPWL the module emits, against a capped double golden.
//   [5] MEMORY SAFETY -- `make test-asan-dhar` rebuilds this under -fsanitize=address,undefined
//                      (buffers sized EXACTLY, never padded, so an OOB index lands outside a real
//                      allocation). The load guard (k<deg) and the phase-Z pre-zero are what keep
//                      the padded-16 block and the dropped >16 pins in bounds.
//
// The synthetic design mirrors Packer.cpp packing (CSR net_ptr, net-major `pins` with net==-1 on
// masked nets, node-major `node_pins` of movable gradient-bearing pins) and ADDS a class of live
// nets with degree in (16, IGNORE_NET_DEGREE] so the cap has something to drop.

#include "modules/hpwl_gradient_dhar.hpp"
#include <vector>
#include <algorithm>
#include <random>
#include <cmath>
#include <cstdio>

using namespace plalgo;

// Production LUT geometry, from host/src/pl_algo/src/main.cpp:244 and Placement.hpp:25.
static constexpr float PLACE_STEP_NORM = 0.05f;
static constexpr int   GAMMA_MULT      = 12;

// ---------------------------------------------------------------------------
// Synthetic design
// ---------------------------------------------------------------------------
struct Design {
    int M = 0, N = 0, num_nets = 0;          // movable, total nodes, nets
    std::vector<coord_t> node_pos;           // [N]
    std::vector<int>     net_ptr;            // [num_nets+1] CSR
    std::vector<NodePin> pins;               // [num_pins] net-major
    std::vector<NodePin> node_pins;          // node-major, movable + gradient-bearing only
    std::vector<int>     pin_to_npin;        // [num_pins] net-major -> node-major slot, -1 if none
    std::vector<int>       masked_pin_idx;   // indices into pins[] belonging to masked nets
    std::vector<int>       oversized_pin_idx; // indices into pins[] on LIVE >16-pin nets
    std::vector<PinOffset> pin_off;          // [num_pins] static, parallel to pins
    std::vector<PinOffset> node_pin_off;     // [num_node_pins] static, parallel to node_pins
};

// Degrees straddle every branch: deg 1 and deg > IGNORE_NET_DEGREE are masked (net=-1); deg 2 is
// the smallest gradient-bearing net; deg 16 sits exactly on the cap (kept); a class of LIVE nets
// with deg in (16, IGNORE] exercises the drop. First and last nets are forced masked so the flush
// at both ends runs on a masked segment.
static Design build_design(unsigned seed) {
    Design d;
    d.M = 4000; d.N = 4800; d.num_nets = 6000;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(0.0f, 10000.0f);
    std::uniform_int_distribution<int>    node_pick(0, d.N - 1);
    std::uniform_int_distribution<int>    deg_pick(2, 16);      // up to and including the cap
    std::uniform_int_distribution<int>    coin(0, 9);
    std::uniform_real_distribution<float> off(-25.0f, 25.0f);

    d.node_pos.resize(d.N);
    for (int n = 0; n < d.N; n++) { d.node_pos[n].x = pos(rng); d.node_pos[n].y = pos(rng); }

    // Nodes [0, 40) are left off every net entirely -> assertion [3] (isolated).
    // Nodes [40, 80) appear ONLY on masked nets    -> assertion [3] (masked-only).
    const int ISOLATED_END = 40, MASKED_ONLY_END = 80;

    d.net_ptr.push_back(0);
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        int deg;
        bool masked_only_net = false;
        bool oversized_net   = false;
        if (net_id == 0 || net_id == d.num_nets - 1) { deg = 1; }          // masked at both ends
        else if (net_id % 500 == 3)  { deg = 140; masked_only_net = true; } // > IGNORE_NET_DEGREE
        else if (net_id % 500 == 7)  { deg = 1;   masked_only_net = true; } // degree <= 1
        else if (net_id % 500 == 11) { deg = 24;  oversized_net   = true; } // LIVE, > 16-pin cap
        else if (net_id % 500 == 17) { deg = 16; }                          // exactly on the cap
        else                         { deg = deg_pick(rng); }

        const int beg = (int)d.pins.size();
        for (int k = 0; k < deg; k++) {
            int nd;
            if (masked_only_net && k < 4) {
                nd = ISOLATED_END + (net_id + k) % (MASKED_ONLY_END - ISOLATED_END);
            } else {
                do { nd = node_pick(rng); } while (nd < MASKED_ONLY_END);
            }
            NodePin r;
            r.node_idx = nd;
            r.x = r.y = 0.0f;                               // filled by refresh_net_pins
            r.net   = net_id;
            d.pins.push_back(r);
            PinOffset o;
            o.off_x = (coin(rng) == 0) ? off(rng) : 0.0f;   // ~10% macro pins carry an offset
            o.off_y = (coin(rng) == 0) ? off(rng) : 0.0f;
            d.pin_off.push_back(o);
        }
        // Packer.cpp:57 -- mask degree <= 1 and degree > IGNORE_NET_DEGREE (XPlace net_mask).
        // NOTE the cap (16) is NOT the mask here: the host still packs 17..IGNORE nets as LIVE, so
        // their movable pins reach node_pins. The MODULE drops them; the test proves it.
        if (deg <= 1 || deg > IGNORE_NET_DEGREE)
            for (int p = beg; p < (int)d.pins.size(); p++) {
                d.pins[p].net = -1;
                d.masked_pin_idx.push_back(p);
            }
        else if (oversized_net)
            for (int p = beg; p < (int)d.pins.size(); p++)
                d.oversized_pin_idx.push_back(p);
        d.net_ptr.push_back((int)d.pins.size());
    }

    // Packer.cpp:70 -- node-major stream: movable, gradient-bearing pins, stable-sorted by node.
    std::vector<int> order;
    for (int p = 0; p < (int)d.pins.size(); p++)
        if (d.pins[p].net >= 0 && d.pins[p].node_idx < d.M) order.push_back(p);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return d.pins[a].node_idx < d.pins[b].node_idx; });
    for (int p : order) { d.node_pins.push_back(d.pins[p]); d.node_pin_off.push_back(d.pin_off[p]); }

    // Static scatter permutation: inverse of `order`. Pins with no gradient slot (masked net or
    // fixed node) stay -1. NOTE: >16-pin LIVE pins on movable nodes DO get a slot here (the host
    // does not know the cap), so the module must actively zero them -- assertion [CAP].
    d.pin_to_npin.assign(d.pins.size(), -1);
    for (int i = 0; i < (int)order.size(); i++) d.pin_to_npin[order[i]] = i;
    return d;
}

static void refresh(Design& d) {
    refresh_net_pins(d.node_pos.data(), d.pin_off.data(), d.pins.data(), (int)d.pins.size());
    refresh_node_pins(d.node_pos.data(), d.node_pin_off.data(), d.node_pins.data(),
                      (int)d.node_pins.size());
}

// ---------------------------------------------------------------------------
// exp back-ends for the golden (independent re-impl of hpwl_lut_exp; interpolation in double).
// ---------------------------------------------------------------------------
static double lut_exp_ref(const std::vector<float>& lut, int lut_size, float inv_lut_step, float d) {
    float idx_f = d * inv_lut_step;
    int   idx   = (int)idx_f;
    if (idx >= lut_size - 1) return 0.0;
    double frac = (double)(idx_f - (float)idx);
    return (double)lut[idx] * (1.0 - frac) + (double)lut[idx + 1] * frac;
}

// ---------------------------------------------------------------------------
// Golden: computeHpwlPartials_CPU (Partials.cpp:200) in double precision, WITH Dhar's 16-cap.
// The only change from hpwl_grad_test's golden is `deg > DHAR_MAX_NET_DEGREE -> skip`.
// ---------------------------------------------------------------------------
static void golden(const Design& d, float inv_gamma, const std::vector<float>& lut,
                   int lut_size, float inv_lut_step, bool use_lut,
                   std::vector<double>& gx, std::vector<double>& gy) {
    gx.assign(d.M, 0.0); gy.assign(d.M, 0.0);
    const double ig = (double)inv_gamma;

    auto E = [&](float dist) -> double {
        return use_lut ? lut_exp_ref(lut, lut_size, inv_lut_step, dist)
                       : std::exp(-(double)dist * ig);
    };

    std::vector<double> Apx, Amx, Apy, Amy;
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        const int beg = d.net_ptr[net_id], end = d.net_ptr[net_id + 1];
        if (beg == end || d.pins[beg].net < 0) continue;         // masked (or empty) net
        const int deg = end - beg;
        if (deg > DHAR_MAX_NET_DEGREE) continue;                 // Dhar cap: ignore large nets

        std::vector<float> px(deg), py(deg);
        float mxx = -1e30f, mnx = 1e30f, mxy = -1e30f, mny = 1e30f;
        for (int k = 0; k < deg; k++) {
            const NodePin& r = d.pins[beg + k];
            px[k] = r.x; py[k] = r.y;
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
            if (nd >= d.M) continue;                 // fixed node: no gradient slot
            const double x = px[k], y = py[k];
            gx[nd] += ((1.0 + x * ig) * Bpx - Cpx * ig) * (Apx[k] * ipx)
                    - ((1.0 - x * ig) * Bmx + Cmx * ig) * (Amx[k] * imx);
            gy[nd] += ((1.0 + y * ig) * Bpy - Cpy * ig) * (Apy[k] * ipy)
                    - ((1.0 - y * ig) * Bmy + Cmy * ig) * (Amy[k] * imy);
        }
    }
}

// Buffers sized EXACTLY (assertion [5] is the sanitizer). pin_grad is poisoned so a dropped scatter
// shows as garbage rather than a lucky zero -- but note the module now PRE-ZEROS it (phase Z), so
// the poison is what a >16 pin's slot must be overwritten from.
static void run_module(const Design& d, float inv_gamma, const std::vector<float>& lut,
                       int lut_size, float inv_lut_step, std::vector<coord_t>& grad,
                       float* hpwl_out = nullptr) {
    std::vector<coord_t> pin_grad(d.node_pins.size(), coord_t{-9.9e30f, -9.9e30f});
    float hpwl_emitted = -1.0f;
    grad.assign(d.M, coord_t{-7.7e30f, -7.7e30f});   // poison; clear_grad must zero the pinless ones
    hpwl_gradient_dhar(d.net_ptr.data(), d.pins.data(), d.node_pins.data(), d.pin_to_npin.data(),
            lut.data(), pin_grad.data(), grad.data(), &hpwl_emitted,
            inv_gamma, inv_lut_step, lut_size, d.num_nets, d.M, (int)d.node_pins.size());
    if (hpwl_out) *hpwl_out = hpwl_emitted;
}

// Golden HPWL: half-perimeter over unmasked nets with degree <= 16 (same cap as the gradient).
static double golden_hpwl(const Design& d) {
    double total = 0.0;
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        const int beg = d.net_ptr[net_id], end = d.net_ptr[net_id + 1];
        if (beg == end || d.pins[beg].net < 0) continue;      // masked (or empty) net
        if (end - beg > DHAR_MAX_NET_DEGREE) continue;        // Dhar cap
        float mxx = -1e30f, mnx = 1e30f, mxy = -1e30f, mny = 1e30f;
        for (int p = beg; p < end; p++) {
            const NodePin& r = d.pins[p];
            mxx = std::max(mxx, r.x); mnx = std::min(mnx, r.x);
            mxy = std::max(mxy, r.y); mny = std::min(mny, r.y);
        }
        total += (double)((mxx - mnx) + (mxy - mny));
    }
    return total;
}

struct Err { double rel_rms, max_rel; };

static Err compare(const std::vector<coord_t>& g, const std::vector<double>& gx,
                   const std::vector<double>& gy) {
    const int M = (int)g.size();
    double se = 0, sr = 0, worst = 0;
    for (int n = 0; n < M; n++) {
        const double ex = (double)g[n].x - gx[n], ey = (double)g[n].y - gy[n];
        se += ex * ex + ey * ey;
        sr += gx[n] * gx[n] + gy[n] * gy[n];
        worst = std::max(worst, std::max(std::fabs(ex), std::fabs(ey)));
    }
    const double rms_ref = std::sqrt(sr / (2.0 * M));
    return Err{ std::sqrt(se / (2.0 * M)) / rms_ref, worst / rms_ref };
}

int main() {
    Design d = build_design(20260909u);
    refresh(d);

    const int lut_size = (int)(GAMMA_MULT / PLACE_STEP_NORM) + 2;      // 242
    std::vector<float> lut(lut_size);
    for (int i = 0; i < lut_size; i++) lut[i] = std::exp(-(float)i * PLACE_STEP_NORM);

    const float gamma        = 120.0f;                                  // mid-run gamma
    const float inv_gamma    = 1.0f / gamma;
    const float inv_lut_step = 1.0f / (PLACE_STEP_NORM * gamma);

    printf("[info] design: %d movable / %d nodes, %d nets, %d pins, %d node_pins, "
           "%d masked pins, %d oversized(>16) pins, lut %d\n",
           d.M, d.N, d.num_nets, (int)d.pins.size(), (int)d.node_pins.size(),
           (int)d.masked_pin_idx.size(), (int)d.oversized_pin_idx.size(), lut_size);

    std::vector<coord_t> grad;
    float hpwl_emitted = -1.0f;
    run_module(d, inv_gamma, lut, lut_size, inv_lut_step, grad, &hpwl_emitted);

    std::vector<double> gx, gy;
    bool ok = true;

    // ---- [1] STRUCTURE: same LUT on both sides; only float rounding (through the tree) remains.
    golden(d, inv_gamma, lut, lut_size, inv_lut_step, /*use_lut=*/true, gx, gy);
    const Err s = compare(grad, gx, gy);
    // The adder tree sums in a different (balanced) order than the golden's sequential double
    // accumulation, so this is looser than hpwl_grad_test's streaming form but of the same order.
    // Bounds are ~10x the observed value; a restructuring bug lands orders of magnitude out.
    const double S_RMS_TOL = 1e-5, S_MAX_TOL = 1e-4;
    if (!(s.rel_rms < S_RMS_TOL && s.max_rel < S_MAX_TOL)) {
        printf("FAIL [1] structure: rel_rms=%.3e (tol %.0e)  max_rel=%.3e (tol %.0e)\n",
               s.rel_rms, S_RMS_TOL, s.max_rel, S_MAX_TOL);
        ok = false;
    }

    // ---- [2] LUT BUDGET: what the exp LUT costs against true exp(). Budget, not correctness.
    golden(d, inv_gamma, lut, lut_size, inv_lut_step, /*use_lut=*/false, gx, gy);
    const Err l = compare(grad, gx, gy);
    const double L_RMS_TOL = 1e-4, L_MAX_TOL = 1e-3;
    if (!(l.rel_rms < L_RMS_TOL && l.max_rel < L_MAX_TOL)) {
        printf("FAIL [2] lut budget: rel_rms=%.3e (tol %.0e)  max_rel=%.3e (tol %.0e)\n",
               l.rel_rms, L_RMS_TOL, l.max_rel, L_MAX_TOL);
        ok = false;
    }

    // ---- [3] ZEROING: nodes with no gradient-bearing pin must be EXACTLY zero (clear_grad).
    std::vector<char> has_grad_pin(d.M, 0);
    for (const NodePin& r : d.node_pins) has_grad_pin[r.node_idx] = 1;
    int zero_checked = 0, zero_bad = 0;
    for (int n = 0; n < d.M; n++) {
        if (has_grad_pin[n]) continue;
        zero_checked++;
        if (grad[n].x != 0.0f || grad[n].y != 0.0f) {
            if (zero_bad < 5)
                printf("FAIL [3] node %d has no gradient pin but grad = {%g, %g}\n",
                       n, grad[n].x, grad[n].y);
            zero_bad++;
        }
    }
    if (zero_checked < 40) { printf("FAIL [3] only %d pinless nodes -- design lost its "
                                   "zero-check coverage\n", zero_checked); ok = false; }
    if (zero_bad) { printf("FAIL [3] %d/%d pinless nodes non-zero\n", zero_bad, zero_checked); ok = false; }

    // ---- [4] MASKING: moving a masked net's pins must not move the gradient at all.
    {
        Design pert = d;
        for (int p : pert.masked_pin_idx) pert.pin_off[p].off_x += 777.0f;
        refresh(pert);
        std::vector<coord_t> grad_pert;
        run_module(pert, inv_gamma, lut, lut_size, inv_lut_step, grad_pert);
        int bad = 0;
        for (int n = 0; n < d.M; n++)
            if (grad_pert[n].x != grad[n].x || grad_pert[n].y != grad[n].y) bad++;
        if (pert.masked_pin_idx.empty()) { printf("FAIL [4] no masked pins in design\n"); ok = false; }
        if (bad) { printf("FAIL [4] perturbing %d masked pins changed %d/%d node gradients\n",
                          (int)pert.masked_pin_idx.size(), bad, d.M); ok = false; }
        printf("[4] masking    %d masked pins perturbed, %d node gradients moved\n",
               (int)pert.masked_pin_idx.size(), bad);
    }

    // ---- [CAP] 16-CAP: moving a LIVE >16-pin net's pins must not move any node gradient. These
    // pins are packed into node_pins (host uses IGNORE_NET_DEGREE=100), so this only holds if the
    // module truly drops them AND zeros their scatter slots. This is the module's headline claim.
    {
        Design pert = d;
        for (int p : pert.oversized_pin_idx) pert.pin_off[p].off_x += 555.0f;
        refresh(pert);
        std::vector<coord_t> grad_pert;
        run_module(pert, inv_gamma, lut, lut_size, inv_lut_step, grad_pert);
        int bad = 0;
        for (int n = 0; n < d.M; n++)
            if (grad_pert[n].x != grad[n].x || grad_pert[n].y != grad[n].y) bad++;
        if (pert.oversized_pin_idx.empty()) {
            printf("FAIL [CAP] no live >16-pin nets in design -- cap has no coverage\n"); ok = false; }
        if (bad) { printf("FAIL [CAP] perturbing %d >16-pin pins changed %d/%d node gradients\n",
                          (int)pert.oversized_pin_idx.size(), bad, d.M); ok = false; }
        printf("[CAP] 16-cap   %d live >16-pin pins perturbed, %d node gradients moved\n",
               (int)pert.oversized_pin_idx.size(), bad);
    }

    // ---- [6] HPWL: the capped HPWL the module emits, against a capped double golden.
    const double hpwl_ref = golden_hpwl(d);
    const double hpwl_emit_rel = std::fabs((double)hpwl_emitted - hpwl_ref) / hpwl_ref;
    const double HE_TOL = 1e-6;   // float narrowing of a double sum dominates (~1e-8 expected)
    if (!(hpwl_emit_rel < HE_TOL)) {
        printf("FAIL [6] emitted HPWL = %.10e, golden = %.10e, rel = %.3e (tol %.0e)\n",
               (double)hpwl_emitted, hpwl_ref, hpwl_emit_rel, HE_TOL);
        ok = false;
    }

    printf("[1] structure  rel_rms=%.3e  max_rel=%.3e   (tol %.0e / %.0e)\n",
           s.rel_rms, s.max_rel, S_RMS_TOL, S_MAX_TOL);
    printf("[2] lut budget rel_rms=%.3e  max_rel=%.3e   (tol %.0e / %.0e)\n",
           l.rel_rms, l.max_rel, L_RMS_TOL, L_MAX_TOL);
    printf("[3] zeroing    %d pinless movable nodes, %d non-zero\n", zero_checked, zero_bad);
    printf("[6] hpwl       emitted=%.8e  golden=%.8e  rel=%.3e (tol %.0e)\n",
           (double)hpwl_emitted, hpwl_ref, hpwl_emit_rel, HE_TOL);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
