// Verify force_gather.hpp (force_gather) -- Stage 5 electrostatic force gather, i.e. the per-node
// density gradient grad(node) = sum_bins overlap_area(node,bin) * eField(bin). This is the ADJOINT
// of density_bin's area scatter and MUST use the identical node_footprint geometry (covered by
// node_footprint_test); the overlap-area weighting is required, not optional (vs DREAMPlace /
// Xplace, sw_only computeElectrostaticForce fixed 2026-07-03).
//
// TWO ASSERTIONS:
//   [1] GATHER   -- module (float acc) vs a double reference that shares the module's node_footprint
//                   and its integer bin ranges, but forms every rectangle overlap and the
//                   field-weighted sum in DOUBLE. Isolates the intersection + accumulation; only
//                   float rounding of the module's running sum remains, so this is the tight check.
//   [2] ADJOINT  -- with eField == 1 everywhere, each node's gathered gx (== gy) must equal the
//                   TOTAL area that node scatters (sum of overlap_area), i.e. its clamped,
//                   area-conserving deposit == real cell area w*h (area conservation), to float
//                   tolerance. This ties the gather to density_bin's scatter without a second model.
//
// Only movable nodes ([0,M)) carry a gradient. The eField sign/scale (lambda, local_density_weight)
// is applied downstream in iteration_update; this module emits the pure area-weighted field sum.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB (guards formats.hpp's HLS headers)
#include "modules/force_gather.hpp"        // the real module (pulls node_footprint.hpp)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace plalgo;                    // NodeBox, coord_t, GRID, node_footprint, force_gather

// Double reference for one node's gather. Uses the REAL node_footprint (float) and the SAME integer
// bin ranges as the module, then forms the overlaps and the field-weighted sum in double.
static void golden_gather(const NodeBox& nd, const std::vector<float>& ef_x,
                          const std::vector<float>& ef_y, float bin_w, float bin_h,
                          double& gx, double& gy) {
    float xl, yl, xh, yh, weight;
    node_footprint(nd, bin_w, bin_h, xl, yl, xh, yh, weight);
    int col_lo = (int)(xl / bin_w); if (col_lo < 0)        col_lo = 0;
    int col_hi = (int)(xh / bin_w); if (col_hi > GRID - 1) col_hi = GRID - 1;
    int row_lo = (int)(yl / bin_h); if (row_lo < 0)        row_lo = 0;
    int row_hi = (int)(yh / bin_h); if (row_hi > GRID - 1) row_hi = GRID - 1;
    gx = 0.0; gy = 0.0;
    for (int col = col_lo; col <= col_hi; col++) {
        const double lx = (double)col * bin_w, rx = lx + bin_w;
        const double ox = std::min((double)xh, rx) - std::max((double)xl, lx);
        if (ox <= 0.0) continue;
        for (int row = row_lo; row <= row_hi; row++) {
            const double ly = (double)row * bin_h, ry = ly + bin_h;
            const double oy = std::min((double)yh, ry) - std::max((double)yl, ly);
            if (oy <= 0.0) continue;
            const double area = ox * oy * (double)weight;
            const int idx = col * GRID + row;
            gx += area * (double)ef_x[idx];
            gy += area * (double)ef_y[idx];
        }
    }
}

int main() {
    const float die = 10000.0f;
    const float bin_w = die / GRID, bin_h = die / GRID;

    std::mt19937 rng(20260828u);
    std::uniform_real_distribution<float> pos(0.0f, 1.0f);
    std::uniform_real_distribution<float> wsmall(2.0f, 40.0f);    // straddles the clamp
    std::uniform_real_distribution<float> wbig(100.0f, 1200.0f);  // multi-bin macros
    std::normal_distribution<float>       fld(0.0f, 1.0f);        // signed field, both directions

    // node_footprint no longer shifts in-die (#20 step 4); legality comes from iteration_update's
    // expanded clamp. So place movable nodes inside that legal region -- a >1-bin margin keeps the
    // centered clamped footprint fully on-grid, matching the real flow, so [2] (area conservation)
    // is not confounded by boundary clipping.
    const float mgn = 2.0f * bin_w;
    const int M = 6000, Nfixed = 20, N = M + Nfixed;
    std::vector<NodeBox> nodes(N);
    for (int i = 0; i < M; i++) {                                 // movable: mostly std cells...
        float w = wsmall(rng), h = wsmall(rng);
        nodes[i] = { mgn + pos(rng) * (die - w - 2 * mgn), mgn + pos(rng) * (die - h - 2 * mgn), w, h };
    }
    for (int i = 0; i < 50 && i < M; i++) {                       // ...plus some movable macros
        float w = wbig(rng), h = wbig(rng);
        nodes[i] = { mgn + pos(rng) * (die - w - 2 * mgn), mgn + pos(rng) * (die - h - 2 * mgn), w, h };
    }
    for (int i = M; i < N; i++) nodes[i] = { 1000.0f, 1000.0f, 200.0f, 200.0f };  // fixed: no grad

    std::vector<float> ef_x(GRID * GRID), ef_y(GRID * GRID);
    for (int i = 0; i < GRID * GRID; i++) { ef_x[i] = fld(rng); ef_y[i] = fld(rng); }

    // ---- run the real module ----
    std::vector<coord_t> grad(M, coord_t{ -7.7e30f, -7.7e30f });   // poison; module writes all M
    force_gather(nodes.data(), ef_x.data(), ef_y.data(), grad.data(), M, bin_w, bin_h);

    // ---- [1] GATHER vs double reference ----
    double se = 0, sr = 0;
    for (int n = 0; n < M; n++) {
        double gx, gy;
        golden_gather(nodes[n], ef_x, ef_y, bin_w, bin_h, gx, gy);
        const double ex = (double)grad[n].x - gx, ey = (double)grad[n].y - gy;
        se += ex * ex + ey * ey;
        sr += gx * gx + gy * gy;
    }
    const double rel_rms = std::sqrt(se / sr);

    // ---- [2] ADJOINT: field == 1 -> gathered value == total scattered area == cell area ----
    std::vector<float> ones(GRID * GRID, 1.0f);
    std::vector<coord_t> gsum(M, coord_t{ 0, 0 });
    force_gather(nodes.data(), ones.data(), ones.data(), gsum.data(), M, bin_w, bin_h);
    double area_max_rel = 0.0;
    for (int n = 0; n < M; n++) {
        const double real_area = (double)nodes[n].w * (double)nodes[n].h;
        if (real_area <= 0.0) continue;
        // gx and gy both equal the summed overlap area (field is 1 on both axes).
        area_max_rel = std::max(area_max_rel, std::fabs((double)gsum[n].x - real_area) / real_area);
    }

    bool ok = true;
    const double G_TOL = 1e-5;    // double-vs-float running sum over up to ~ (macro bins); observed below
    if (!(rel_rms < G_TOL)) { printf("FAIL [1] gather rel_rms=%.3e (tol %.0e)\n", rel_rms, G_TOL); ok = false; }
    // Area conservation limited by (xh-xl) cancellation at absolute coords ~1e4 (see node_footprint_test).
    const double A_TOL = 5e-4;
    if (!(area_max_rel < A_TOL)) { printf("FAIL [2] adjoint/area rel=%.3e (tol %.0e)\n", area_max_rel, A_TOL); ok = false; }

    printf("[1] gather     rel_rms=%.3e (tol %.0e)   [%d movable]\n", rel_rms, G_TOL, M);
    printf("[2] adjoint    area max_rel=%.3e (tol %.0e)  (field==1 -> gathered == cell area)\n", area_max_rel, A_TOL);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
