// Verify hpwl_gradient.hpp (hpwl_CU) against the sw_only WA-HPWL gradient golden.
//
// The golden is computeHpwlPartials_CPU (host/src/sw_only/src/placer/Partials.cpp:200),
// re-derived here in double precision. It is re-derived rather than #included because that
// function is a Placer method wired into DataBase/Net/Logger; pulling it in would drag the
// whole host object graph into a tier-1 harness that is supposed to be pure g++. The formula
// below is a line-for-line transcription -- if Partials.cpp's math changes, this must change
// with it.
//
// SIX ASSERTIONS, because hpwl_CU has two independent ways to be wrong and they need
// separating (a single end-to-end number cannot tell a restructuring bug from LUT error):
//
//   [1] STRUCTURE   -- module (float, LUT) vs golden (double, SAME LUT). Isolates the segmented
//                      reductions, the bbox, the net-major -> node-major transposition, the WA
//                      quotient rule and the net masking. Only float rounding is left, so this
//                      is the tight one and the one that catches a restructuring bug.
//   [2] LUT BUDGET  -- module (float, LUT) vs golden (double, true exp()). This is the module's
//                      own "NOTE (accuracy watch)" comment turned into a check. It is a budget,
//                      not a correctness bound: it says how much the exp LUT costs, and fails if
//                      that cost moves.
//   [3] ZEROING     -- movable nodes with no gradient-bearing pin must come back EXACTLY {0,0}.
//                      Bit-exact; this is what clear_grad is for. The design below deliberately
//                      contains such nodes (isolated, and masked-net-only).
//   [4] MASKING     -- total gradient must be unchanged when a masked net's pins are perturbed.
//   [6] HPWL/BBOX   -- phase 1's bounding boxes must reduce to the correct HPWL. This is the P1b
//                      prerequisite (REPORT_20): P1b deletes metrics::hpwl_sweep, a fourth pass
//                      over net_pins recomputing a bounding box sweep_bbox already has, which is
//                      only sound if the two agree -- same nets, same mask, same pin positions.
//                      Asserted here BEFORE P1b is implemented, so the gate exists first.
//   [5] MEMORY SAFETY -- `make test-asan` rebuilds this same file under
//                      -fsanitize=address,undefined. It is a separate target because it is a
//                      bounds check, not a value check, and nothing here can substitute for it:
//                      dropping either `if (r.net < 0) continue;` leaves the gradient
//                      BIT-IDENTICAL (masked nets never reach node_pins, and both scratch writes
//                      are already guarded by `>= 0`), so [1]-[4] all pass. What it actually does
//                      is index bb_DDR[-1], whose garbage bbox then drives a NEGATIVE index into
//                      lut_BRAM -- hpwl_lut_exp bounds `idx` from above but not from below.
//                      That lower bound is one-sided by assumption (today every distance handed
//                      to it is >= 0 by construction); a phase-1/2 fusion that let phase 2 see a
//                      partial bounding box would make it a live bug.
//
// MUTATION COVERAGE (2026-08-28). Each of these was applied to hpwl_gradient.hpp and the suite
// re-run, because a harness that has never failed proves nothing:
//   sign flip in the WA partial          -> [1][2]      wrong bbox read in phase 3 -> [1][2][4]
//   clear_grad deleted                   -> [1][2][3]   phase 2 given a stale bbox -> [1][2][4]
//   final segment flush dropped          -> [1][2]      LUT interpolation dropped  -> [1][2]
//   net mask dropped in sweep_sums       -> [5] ONLY
//   sweep_bbox last-net flush dropped    -> [6] ONLY
//   sweep_bbox y-extent perturbed by 1.0 -> [6] ONLY. Worth understanding: phases 2 and 4 both
//     read the same corrupted bb_DDR, so the gradient stays self-consistent, and the WA partial
//     is smooth enough in the bounding box that a 1.0-unit error on a 10000-unit die lands
//     inside [1]'s tolerance. HPWL is a direct sum of extents and has no such slack. [6] is
//     therefore not redundant with [1] -- it is strictly more sensitive to the bbox itself.
//   net mask dropped in sweep_bbox       -> NOTHING, correctly: unlike sweep_sums, sweep_bbox
//     never indexes bb_DDR by r.net, and its one write is already guarded by `bb_net >= 0`, so
//     letting masked pins through is genuinely a no-op there. The two `continue`s are NOT
//     symmetric -- sweep_bbox's is an optimization, sweep_sums's is load-bearing.
//
// The synthetic design mirrors the packing rules in host/src/pl_algo/src/Packer.cpp:42-74
// (CSR net_ptr, net-major `pins` with net==-1 on masked nets, node-major `node_pins` holding
// only movable gradient-bearing pins). Those rules are replicated, not called, for the same
// reason as the golden -- so a Packer change must be mirrored here.

#include "modules/hpwl_gradient.hpp"
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
    std::vector<int>     masked_pin_idx;     // indices into pins[] belonging to masked nets
};

// Degrees are chosen to straddle every branch hpwl_CU has: deg 1 and deg > IGNORE_NET_DEGREE
// are masked (net = -1), deg 2 is the smallest gradient-bearing net, and the first and last
// nets are forced masked so the segmented flush at both ends is exercised on a masked segment.
static Design build_design(unsigned seed) {
    Design d;
    d.M = 4000; d.N = 4800; d.num_nets = 6000;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(0.0f, 10000.0f);
    std::uniform_int_distribution<int>    node_pick(0, d.N - 1);
    std::uniform_int_distribution<int>    deg_pick(2, 8);
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
        if (net_id == 0 || net_id == d.num_nets - 1) { deg = 1; }          // masked at both ends
        else if (net_id % 500 == 3)  { deg = 140; masked_only_net = true; } // > IGNORE_NET_DEGREE
        else if (net_id % 500 == 7)  { deg = 1;   masked_only_net = true; } // degree <= 1
        else                         { deg = deg_pick(rng); }

        const int beg = (int)d.pins.size();
        for (int k = 0; k < deg; k++) {
            int nd;
            if (masked_only_net && k < 4) {
                // Seed masked nets with the masked-only nodes so those nodes have pins that
                // exist in `pins` but must never reach `node_pins` or the gradient.
                nd = ISOLATED_END + (net_id + k) % (MASKED_ONLY_END - ISOLATED_END);
            } else {
                do { nd = node_pick(rng); } while (nd < MASKED_ONLY_END);
            }
            NodePin r;
            r.node_idx = nd;
            r.off_x = (coin(rng) == 0) ? off(rng) : 0.0f;   // ~10% macro pins carry an offset
            r.off_y = (coin(rng) == 0) ? off(rng) : 0.0f;
            r.net   = net_id;
            d.pins.push_back(r);
        }
        // Packer.cpp:57 -- mask degree <= 1 and degree > IGNORE_NET_DEGREE (XPlace net_mask).
        if (deg <= 1 || deg > IGNORE_NET_DEGREE)
            for (int p = beg; p < (int)d.pins.size(); p++) {
                d.pins[p].net = -1;
                d.masked_pin_idx.push_back(p);
            }
        d.net_ptr.push_back((int)d.pins.size());
    }

    // Packer.cpp:70 -- node-major stream: movable, gradient-bearing pins, stable-sorted by node.
    for (const NodePin& r : d.pins)
        if (r.net >= 0 && r.node_idx < d.M) d.node_pins.push_back(r);
    std::stable_sort(d.node_pins.begin(), d.node_pins.end(),
                     [](const NodePin& a, const NodePin& b) { return a.node_idx < b.node_idx; });
    return d;
}

// ---------------------------------------------------------------------------
// exp back-ends for the golden
// ---------------------------------------------------------------------------
// Independent re-implementation of hpwl_lut_exp. Index and fraction are formed in FLOAT so they
// select the same table entries the module selects; only the interpolation runs in double. That
// keeps assertion [1] measuring the reduction structure rather than LUT index jitter.
static double lut_exp_ref(const std::vector<float>& lut, int lut_size, float inv_lut_step, float d) {
    float idx_f = d * inv_lut_step;
    int   idx   = (int)idx_f;
    if (idx >= lut_size - 1) return 0.0;
    double frac = (double)(idx_f - (float)idx);
    return (double)lut[idx] * (1.0 - frac) + (double)lut[idx + 1] * frac;
}

// ---------------------------------------------------------------------------
// Golden: computeHpwlPartials_CPU (Partials.cpp:200) in double precision.
// use_lut selects the exp back-end; everything else is identical between the two calls.
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
        if (beg == end || d.pins[beg].net < 0) continue;   // masked (or empty) net

        // Pin positions are formed in FLOAT exactly as the module does, so the bounding box is
        // bit-identical and the comparison is not polluted by a differing max/min.
        const int deg = end - beg;
        std::vector<float> px(deg), py(deg);
        float mxx = -1e30f, mnx = 1e30f, mxy = -1e30f, mny = 1e30f;
        for (int k = 0; k < deg; k++) {
            const NodePin& r = d.pins[beg + k];
            px[k] = d.node_pos[r.node_idx].x + r.off_x;
            py[k] = d.node_pos[r.node_idx].y + r.off_y;
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

// ---------------------------------------------------------------------------
// Buffers are sized EXACTLY, never padded, because assertion [5] is the sanitizer: an
// out-of-bounds index has to land outside a real allocation for ASan to see it.
// bb_out, when given, receives the per-net bounding boxes phase 1 wrote -- that is what
// assertion [6] reduces to an HPWL.
static void run_module(const Design& d, float inv_gamma, const std::vector<float>& lut,
                       int lut_size, float inv_lut_step, std::vector<coord_t>& grad,
                       std::vector<NetBBox>* bb_out = nullptr) {
    std::vector<NetBBox> bb(d.num_nets);
    std::vector<NetSums> sums(d.num_nets);
    // Poison the output, do NOT pre-zero it: clear_grad is the module's own zeroing pass and
    // assertion [3] is only meaningful if the harness is not doing that job for it.
    grad.assign(d.M, coord_t{-7.7e30f, -7.7e30f});
    hpwl_CU(d.node_pos.data(), d.net_ptr.data(), d.pins.data(), d.node_pins.data(),
            lut.data(), bb.data(), sums.data(), grad.data(),
            inv_gamma, inv_lut_step, lut_size,
            d.num_nets, d.M, (int)d.node_pins.size());
    if (bb_out) *bb_out = bb;
}

// Golden HPWL: sum over unmasked nets of the pin bounding box half-perimeter. Mirrors
// DataBase::computeTotalWirelength("HPWL") and hpwlFromPacked (Placement.hpp:146). Pin positions
// are formed in float exactly as the module forms them, so the bbox is bit-identical and only
// the accumulation is promoted to double -- same as metrics.hpp, which sums in double on purpose.
static double golden_hpwl(const Design& d) {
    double total = 0.0;
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        const int beg = d.net_ptr[net_id], end = d.net_ptr[net_id + 1];
        if (beg == end || d.pins[beg].net < 0) continue;      // masked (or empty) net
        float mxx = -1e30f, mnx = 1e30f, mxy = -1e30f, mny = 1e30f;
        for (int p = beg; p < end; p++) {
            const NodePin& r = d.pins[p];
            const float x = d.node_pos[r.node_idx].x + r.off_x;
            const float y = d.node_pos[r.node_idx].y + r.off_y;
            mxx = std::max(mxx, x); mnx = std::min(mnx, x);
            mxy = std::max(mxy, y); mny = std::min(mny, y);
        }
        total += (double)((mxx - mnx) + (mxy - mny));
    }
    return total;
}

struct Err { double rel_rms, max_rel; };

// Errors are normalized by the GLOBAL gradient RMS, not per element. The WA partial is a
// difference of two nearly equal terms for a node near its net's centre, so per-element
// relative error is unbounded there and would only measure cancellation, not correctness.
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
    Design d = build_design(20260828u);

    const int lut_size = (int)(GAMMA_MULT / PLACE_STEP_NORM) + 2;      // 242
    std::vector<float> lut(lut_size);
    for (int i = 0; i < lut_size; i++) lut[i] = std::exp(-(float)i * PLACE_STEP_NORM);

    const float gamma        = 120.0f;                                  // mid-run gamma
    const float inv_gamma    = 1.0f / gamma;
    const float inv_lut_step = 1.0f / (PLACE_STEP_NORM * gamma);

    printf("[info] design: %d movable / %d nodes, %d nets, %d pins, %d node_pins, lut %d\n",
           d.M, d.N, d.num_nets, (int)d.pins.size(), (int)d.node_pins.size(), lut_size);

    std::vector<coord_t> grad;
    std::vector<NetBBox> bb;
    run_module(d, inv_gamma, lut, lut_size, inv_lut_step, grad, &bb);

    std::vector<double> gx, gy;
    bool ok = true;

    // ---- [1] STRUCTURE: same LUT on both sides; only float rounding remains.
    golden(d, inv_gamma, lut, lut_size, inv_lut_step, /*use_lut=*/true, gx, gy);
    const Err s = compare(grad, gx, gy);
    // Observed 2026-08-28: rel_rms 1.32e-6, max_rel 1.13e-5 -- float rounding through a
    // degree-2..8 reduction plus the cancellation in the final subtraction. Bounds are ~10x
    // observed: a restructuring bug here is orders of magnitude (the mutation checks in
    // README.md land at 1e-1..1e0), so a tighter bound would only buy flakiness.
    const double S_RMS_TOL = 1e-5, S_MAX_TOL = 1e-4;
    if (!(s.rel_rms < S_RMS_TOL && s.max_rel < S_MAX_TOL)) {
        printf("FAIL [1] structure: rel_rms=%.3e (tol %.0e)  max_rel=%.3e (tol %.0e)\n",
               s.rel_rms, S_RMS_TOL, s.max_rel, S_MAX_TOL);
        ok = false;
    }

    // ---- [2] LUT BUDGET: what the exp LUT costs against true exp(). Budget, not correctness.
    golden(d, inv_gamma, lut, lut_size, inv_lut_step, /*use_lut=*/false, gx, gy);
    const Err l = compare(grad, gx, gy);
    // Observed 2026-08-28: rel_rms 1.30e-5, max_rel 9.66e-5 at PLACE_STEP_NORM=0.05, GAMMA_MULT=12.
    // Note this is ~24x BETTER than the h^2/8 = 3.1e-4 relative error of linearly interpolating
    // exp at h=0.05: the WA partial is a ratio of sums of those exponentials, and the LUT errors
    // are strongly correlated across a net's pins, so most of it cancels. That is a measured
    // observation, not a guarantee -- hence the check. Bounds are ~10x observed, so this trips if
    // someone coarsens PLACE_STEP_NORM or shortens GAMMA_MULT without meaning to.
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
    Design pert = d;
    for (int p : pert.masked_pin_idx) pert.pins[p].off_x += 777.0f;   // only masked nets shift
    std::vector<coord_t> grad_pert;
    run_module(pert, inv_gamma, lut, lut_size, inv_lut_step, grad_pert);
    int mask_bad = 0;
    for (int n = 0; n < d.M; n++)
        if (grad_pert[n].x != grad[n].x || grad_pert[n].y != grad[n].y) mask_bad++;
    if (pert.masked_pin_idx.empty()) { printf("FAIL [4] no masked pins in design\n"); ok = false; }
    if (mask_bad) { printf("FAIL [4] perturbing %d masked pins changed %d/%d node gradients\n",
                           (int)pert.masked_pin_idx.size(), mask_bad, d.M); ok = false; }

    // ---- [6] HPWL-FROM-BBOX: phase 1's bounding boxes must reduce to the correct HPWL.
    // This is the P1b prerequisite (REPORT_20_hpwl_gradient_opt_20260828). P1b proposes emitting
    // HPWL from the bbox pass and deleting metrics::hpwl_sweep, which is a fourth pass over
    // net_pins recomputing a bounding box sweep_bbox already has. That is only sound if
    // sweep_bbox's bbox IS the HPWL bbox -- same nets, same mask, same pin positions. Asserted
    // here against a double golden, today, before any of it is implemented. When P1b lands, this
    // assertion retargets from bb_DDR to the emitted scalar and the golden does not change.
    // metrics.hpp itself cannot be #included here: it pulls formats.hpp -> ap_int.h/hls_stream.h,
    // which a pure-g++ tier-1 harness does not have.
    double hpwl_from_bb = 0.0;
    for (int net_id = 0; net_id < d.num_nets; net_id++) {
        const int beg = d.net_ptr[net_id], end = d.net_ptr[net_id + 1];
        if (beg == end || d.pins[beg].net < 0) continue;   // masked: bb[net] never written
        hpwl_from_bb += (double)((bb[net_id].mxx - bb[net_id].mnx) +
                                 (bb[net_id].mxy - bb[net_id].mny));
    }
    const double hpwl_ref = golden_hpwl(d);
    const double hpwl_rel = std::fabs(hpwl_from_bb - hpwl_ref) / hpwl_ref;
    // Observed 2026-08-28: 0.0 exactly. The bounding box is a min/max over the same float pin
    // positions, so it is bit-identical, and both sides accumulate in double in net order. The
    // bound is not 0 only because a future reassociation of the sum (P3) is legitimate.
    const double H_TOL = 1e-12;
    if (!(hpwl_rel < H_TOL)) {
        printf("FAIL [6] HPWL from bb_DDR = %.10e, golden = %.10e, rel = %.3e (tol %.0e)\n",
               hpwl_from_bb, hpwl_ref, hpwl_rel, H_TOL);
        ok = false;
    }

    printf("[1] structure  rel_rms=%.3e  max_rel=%.3e   (tol %.0e / %.0e)\n",
           s.rel_rms, s.max_rel, S_RMS_TOL, S_MAX_TOL);
    printf("[2] lut budget rel_rms=%.3e  max_rel=%.3e   (tol %.0e / %.0e)\n",
           l.rel_rms, l.max_rel, L_RMS_TOL, L_MAX_TOL);
    printf("[3] zeroing    %d pinless movable nodes, %d non-zero\n", zero_checked, zero_bad);
    printf("[4] masking    %d masked pins perturbed, %d node gradients moved\n",
           (int)pert.masked_pin_idx.size(), mask_bad);
    printf("[6] hpwl/bbox  from bb_DDR=%.8e  golden=%.8e  rel=%.3e (tol %.0e)\n",
           hpwl_from_bb, hpwl_ref, hpwl_rel, H_TOL);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
