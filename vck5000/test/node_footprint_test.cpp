// Verify node_footprint.hpp (node_footprint) -- the shared density footprint geometry that both
// density_bin's scatter and force_gather's gather depend on. Covering it once anchors both.
//
// node_footprint clamps a sub-bin cell up to >= sqrt(2) bins per dimension, weights its deposit by
// weight = real_area/clamped_area (area-conserving smoothing = XPlace expand_ratio), centres the
// footprint on the cell, then shifts it to stay on-grid. Macros already exceed the clamp, so they
// pass through unchanged (weight 1).
//
// FOUR ASSERTIONS:
//   [1] SPEC       -- module (float) vs an independent double re-derivation of the SAME spec.
//                     Isolates the clamp/centre/shift structure; only float rounding remains, so
//                     this is the tight one that catches a restructuring or sign bug.
//   [2] AREA       -- weight * clamped_area == real_area (w*h). The invariant the smoothing must
//                     satisfy no matter how the box is placed; independent of [1]'s re-derivation.
//   [3] CENTERING  -- the footprint is CENTERED on the cell: (xl+xh)/2 == cell centre x + w/2, and
//                     likewise y. This is the property the no-shift geometry guarantees (a
//                     reintroduced in-die shift would break it for edge cells); it holds regardless
//                     of position, so it is driven at the die edges too.
//   [4] KIND SPLIT -- an interior macro (already bigger than sqrt(2) bins) gets NO area smoothing,
//                     so its weight is decided purely by the #11b override: NOT a movable macro ->
//                     1.0 BIT-EXACT (x/x == 1.0 in float); movable macro with td < 1 -> td exactly;
//                     movable macro with td == 1 -> 1.0 (override gated off). All bit-exact. The
//                     coords are NOT bit-exact (xl = x + 0.5w - 0.5w != x in float); [1] covers them.
//
// DIVERGENCES from the sw_only golden computeNodeFootprint (Grid.cpp:9):
//   * in-die shift -- RESOLVED 2026-08-29 (#20 step 4). The PL module no longer shifts; it centres
//     the footprint exactly like computeNodeFootprint, and iteration_update's expanded-box clamp
//     (Step.cpp:131 enforceDieBoundaries) keeps it in-die. The golden above and [3] now assert that.
//   * movable-macro weight override -- IMPLEMENTED 2026-08-29 (#20 step 5, with fillers). sw_only
//     overwrites weight with target_density for a MOVABLE MACRO when td < 1 (Grid.cpp:31-32; the
//     is_mov_macro rule is Setup.cpp:106 tagMovableMacros = is_tall && is_large && is_sized). It is
//     driven through node_footprint's (is_movable_macro, target_density) params, which a caller
//     derives from the node's index range (host_interface.hpp classifyNode). [4] exercises the
//     branch directly; the override stays LATENT on std-cell designs (num_movable_macros == 0).

#include "tier1_stub.hpp"                  // PL_TIER1_STUB (guards formats.hpp's HLS headers)
#include "modules/node_footprint.hpp"      // the real module
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace plalgo;                    // NodeBox, GRID, node_footprint, ENABLE_DENSITY_CLAMP

struct FP { double xl, yl, xh, yh, weight; };

// Independent double re-derivation of node_footprint's documented spec. Uses the true sqrt(2),
// not the module's float literal 1.41421356f, so it is a genuine reference and not a copy -- the
// resulting ~1e-7 difference in the clamp threshold is well inside [1]'s tolerance.
static FP golden(const NodeBox& nd, double bin_w, double bin_h,
                 bool is_movable_macro = false, double target_density = 1.0) {
    const double w = nd.w, h = nd.h;
    double cw = w, ch = h, weight = 1.0;
    if (ENABLE_DENSITY_CLAMP) {
        const double min_w = bin_w * M_SQRT2, min_h = bin_h * M_SQRT2;
        cw = w > min_w ? w : min_w;
        ch = h > min_h ? h : min_h;
        weight = (cw > 0.0 && ch > 0.0) ? (w * h) / (cw * ch) : 0.0;
        if (target_density < 1.0 && is_movable_macro) weight = target_density;   // #11b override
    }
    const double xl = nd.x + 0.5 * w - 0.5 * cw;   // centered, NO in-die shift (computeNodeFootprint)
    const double yl = nd.y + 0.5 * h - 0.5 * ch;
    return FP{ xl, yl, xl + cw, yl + ch, weight };
}

static FP run(const NodeBox& nd, float bin_w, float bin_h,
              bool is_movable_macro = false, float target_density = 1.0f) {
    float xl, yl, xh, yh, weight;
    node_footprint(nd, bin_w, bin_h, is_movable_macro, target_density, xl, yl, xh, yh, weight);
    return FP{ xl, yl, xh, yh, weight };
}

int main() {
    const float die = 10000.0f;
    const float bin_w = die / GRID, bin_h = die / GRID;   // ~9.77
    const float grid_w = GRID * bin_w, grid_h = GRID * bin_h;

    std::mt19937 rng(20260828u);
    std::uniform_real_distribution<float> pos(0.0f, 1.0f);
    std::uniform_real_distribution<float> wsmall(1.0f, 30.0f);     // straddles the ~13.8 clamp
    std::uniform_real_distribution<float> wbig(200.0f, 1500.0f);   // always macros

    std::vector<NodeBox> cases;
    // Random interior std cells and macros (margin from the die edge so [1]'s float/double branch
    // decisions cannot diverge on the shift threshold).
    int clamp_fired = 0, passthrough_seen = 0;
    for (int i = 0; i < 4000; i++) {
        float w = wsmall(rng), h = wsmall(rng);
        cases.push_back({ 100.0f + pos(rng) * (die - 200.0f - w), 100.0f + pos(rng) * (die - 200.0f - h), w, h });
        if (w < bin_w * 1.41421356f || h < bin_h * 1.41421356f) clamp_fired++;
    }
    for (int i = 0; i < 200; i++) {
        float w = wbig(rng), h = wbig(rng);
        cases.push_back({ 2000.0f + pos(rng) * 4000.0f, 2000.0f + pos(rng) * 4000.0f, w, h });
        passthrough_seen++;
    }
    // Designed edge cells: a sub-bin cell pinned to each die edge. With the shift gone these test
    // that centering (and the spec) still hold at the boundary -- the footprint is allowed to reach
    // past the die here (the upstream clamp is what keeps real positions legal; see the header).
    const float s = 3.0f;                                 // sub-bin -> clamp inflates cw > w
    cases.push_back({ grid_w - s, 5000.0f,     s, s });   // +x edge
    cases.push_back({ 5000.0f,    grid_h - s,  s, s });   // +y edge
    cases.push_back({ 0.0f,       5000.0f,     s, s });   // -x edge
    cases.push_back({ 5000.0f,    0.0f,        s, s });   // -y edge
    cases.push_back({ 0.0f,       0.0f,        s, s });   // corner (both)
    cases.push_back({ 5000.0f,    5000.0f,     0.0f, 0.0f }); // degenerate: weight -> 0 guard

    bool ok = true;

    // ---- [1] SPEC + [2] AREA over every case ----
    double spec_max_rel = 0.0, area_max_rel = 0.0;
    for (const NodeBox& nd : cases) {
        FP g = golden(nd, bin_w, bin_h), m = run(nd, bin_w, bin_h);
        const double scale = (double)die;                // normalise coord error by die extent
        auto rel = [&](double a, double b) { return std::fabs(a - b) / scale; };
        spec_max_rel = std::max({ spec_max_rel, rel(m.xl, g.xl), rel(m.yl, g.yl),
                                                rel(m.xh, g.xh), rel(m.yh, g.yh) });
        spec_max_rel = std::max(spec_max_rel, std::fabs(m.weight - g.weight));  // weight in [0,1]
        // area conservation: weight * clamped box area == real area (skip the degenerate 0-area).
        const double real_area = (double)nd.w * (double)nd.h;
        if (real_area > 0.0) {
            const double deposited = m.weight * (m.xh - m.xl) * (m.yh - m.yl);
            area_max_rel = std::max(area_max_rel, std::fabs(deposited - real_area) / real_area);
        }
    }
    // AREA_TOL is loose because the deposited area is reconstructed from (xh - xl), and xl/xh are
    // ABSOLUTE coords up to ~1e4: at float precision that difference loses ~1e-3 relative on a
    // ~14-wide box (catastrophic cancellation, not a module bug). Observed 2026-08-28: 4.98e-5;
    // bound is ~10x. The invariant still catches a wrong weight formula. [1] carries the tight check.
    const double SPEC_TOL = 1e-5, AREA_TOL = 5e-4;
    if (!(spec_max_rel < SPEC_TOL)) { printf("FAIL [1] spec max_rel=%.3e (tol %.0e)\n", spec_max_rel, SPEC_TOL); ok = false; }
    if (!(area_max_rel < AREA_TOL)) { printf("FAIL [2] area  max_rel=%.3e (tol %.0e)\n", area_max_rel, AREA_TOL); ok = false; }

    // ---- [3] CENTERING: footprint centre == cell centre, for every case (edges included) ----
    // xl = x + 0.5w - 0.5cw and xh = xl + cw, so (xl+xh)/2 must equal x + 0.5w. Reconstructed from
    // absolute coords, so the same ~1e-4 cancellation as [2] applies -> normalise by die.
    double center_max = 0.0;
    for (const NodeBox& nd : cases) {
        FP m = run(nd, bin_w, bin_h);
        const double cxf = 0.5 * (m.xl + m.xh), cyf = 0.5 * (m.yl + m.yh);
        const double cxc = (double)nd.x + 0.5 * nd.w, cyc = (double)nd.y + 0.5 * nd.h;
        center_max = std::max(center_max, std::max(std::fabs(cxf - cxc), std::fabs(cyf - cyc)) / die);
    }
    const double CENTER_TOL = 1e-5;
    if (!(center_max < CENTER_TOL)) { printf("FAIL [3] centering max_rel=%.3e (tol %.0e)\n", center_max, CENTER_TOL); ok = false; }

    // ---- [4] KIND SPLIT: macro passthrough vs the #11b movable-macro deposit-weight override ----
    // A macro-sized cell gets NO area smoothing (cw==w, ch==h). What its weight ends up being now
    // depends on kind (host_interface.hpp classifyNode), driven through the two new params:
    //   * NOT a movable macro (std-cell/filler/fixed), any td      -> weight EXACTLY 1.0 (passthrough)
    //   * movable macro, td < 1 (here 0.7)                         -> weight EXACTLY td (override)
    //   * movable macro, td == 1 (override gated off)              -> weight EXACTLY 1.0
    // All three are bit-exact: the override is a direct assignment and passthrough is x/x==1.0.
    const float TD = 0.7f;
    int pass_bad = 0, override_bad = 0, td1_bad = 0;
    for (const NodeBox& nd : cases) {
        if (nd.w <= bin_w * 1.41421356f || nd.h <= bin_h * 1.41421356f) continue;  // not a macro
        if (run(nd, bin_w, bin_h, /*is_macro*/false, /*td*/TD).weight != 1.0f) pass_bad++;
        if (run(nd, bin_w, bin_h, /*is_macro*/true,  /*td*/TD).weight != TD)   override_bad++;
        if (run(nd, bin_w, bin_h, /*is_macro*/true,  /*td*/1.0f).weight != 1.0f) td1_bad++;
    }
    if (passthrough_seen < 100) { printf("FAIL [4] only %d macros -- lost passthrough coverage\n", passthrough_seen); ok = false; }
    if (pass_bad)     { printf("FAIL [4] %d non-macro cells not weight 1.0 passthrough\n", pass_bad); ok = false; }
    if (override_bad) { printf("FAIL [4] %d movable macros did not deposit at td=%.2f\n", override_bad, TD); ok = false; }
    if (td1_bad)      { printf("FAIL [4] %d movable macros at td=1.0 not weight 1.0 (override should gate off)\n", td1_bad); ok = false; }

    printf("[1] spec       max_rel=%.3e (tol %.0e)   [%d clamp-firing cells]\n", spec_max_rel, SPEC_TOL, clamp_fired);
    printf("[2] area       max_rel=%.3e (tol %.0e)\n", area_max_rel, AREA_TOL);
    printf("[3] centering  max_rel=%.3e (tol %.0e)  (footprint centre == cell centre, edges incl.)\n", center_max, CENTER_TOL);
    printf("[4] kind       %d macros: passthru bad=%d, override(td=%.2f) bad=%d, td1 bad=%d\n",
           passthrough_seen, pass_bad, TD, override_bad, td1_bad);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
