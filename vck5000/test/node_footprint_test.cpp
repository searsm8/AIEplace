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
//   [3] ON-GRID    -- for a cell whose clamped footprint fits in the die, 0 <= xl and xh <= grid_w
//                     (and y). Drives the in-die shift on all four edges (a dropped shift escapes
//                     the grid and indexes a bin off the end in the callers).
//   [4] PASSTHROUGH-- an interior macro (already bigger than sqrt(2) bins) gets NO smoothing:
//                     weight == 1.0f BIT-EXACT (cw==w, ch==h, so weight = w*h/(w*h), an exact
//                     x/x == 1.0 in float). A stray inflation or weight!=1 is caught exactly. The
//                     coords are NOT bit-exact (xl = x + 0.5w - 0.5w != x in float); [1] covers
//                     them against the double golden.
//
// KNOWN DIVERGENCES from the sw_only golden computeNodeFootprint (Grid.cpp:9), both TODO #20
// step 4, deliberately NOT asserted here because the module has not yet been changed to match:
//   * sw_only does NO in-die shift (Grid.cpp:35-37) -- enforceDieBoundaries pre-projects nodes so
//     the footprint is legal by construction; the PL module shifts instead. Assertion [3] tests
//     the PL behaviour, which is a SUPERSET (shift only moves an already-legal box less).
//   * sw_only overwrites weight with target_density for a movable macro when td < 1
//     (Grid.cpp:31-32, TODO #11b); the PL module has no movable-macro concept in v1 (no filler /
//     macro flag crosses the boundary). When step 4 lands that, add a [5] here.
// When step 4 reconciles these, this file is where the golden retargets to computeNodeFootprint.

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
static FP golden(const NodeBox& nd, double bin_w, double bin_h) {
    const double w = nd.w, h = nd.h;
    double cw = w, ch = h, weight = 1.0;
    if (ENABLE_DENSITY_CLAMP) {
        const double min_w = bin_w * M_SQRT2, min_h = bin_h * M_SQRT2;
        cw = w > min_w ? w : min_w;
        ch = h > min_h ? h : min_h;
        weight = (cw > 0.0 && ch > 0.0) ? (w * h) / (cw * ch) : 0.0;
    }
    const double grid_w = (double)GRID * bin_w, grid_h = (double)GRID * bin_h;
    double xl = nd.x + 0.5 * w - 0.5 * cw;
    double yl = nd.y + 0.5 * h - 0.5 * ch;
    if (xl + cw > grid_w) xl = grid_w - cw;
    if (yl + ch > grid_h) yl = grid_h - ch;
    if (xl < 0.0) xl = 0.0;
    if (yl < 0.0) yl = 0.0;
    return FP{ xl, yl, xl + cw, yl + ch, weight };
}

static FP run(const NodeBox& nd, float bin_w, float bin_h) {
    float xl, yl, xh, yh, weight;
    node_footprint(nd, bin_w, bin_h, xl, yl, xh, yh, weight);
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
    // Designed edge cells: a sub-bin cell pinned to each die edge forces the in-die shift there.
    const float s = 3.0f;                                 // sub-bin -> clamp inflates -> must shift
    cases.push_back({ grid_w - s, 5000.0f,     s, s });   // +x edge
    cases.push_back({ 5000.0f,    grid_h - s,  s, s });   // +y edge
    cases.push_back({ 0.0f,       5000.0f,     s, s });   // -x edge (centre shift pushes xl<0)
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

    // ---- [3] ON-GRID for every case (all fit in the die) ----
    int off_grid = 0;
    for (const NodeBox& nd : cases) {
        FP m = run(nd, bin_w, bin_h);
        if (m.xl < 0.0 || m.yl < 0.0 || m.xh > (double)grid_w + 1e-3 || m.yh > (double)grid_h + 1e-3)
            off_grid++;
    }
    if (off_grid) { printf("FAIL [3] %d footprints left the grid\n", off_grid); ok = false; }

    // ---- [4] PASSTHROUGH: interior macros come back bit-exact ----
    int pass_bad = 0;
    for (const NodeBox& nd : cases) {
        if (nd.w <= bin_w * 1.41421356f || nd.h <= bin_h * 1.41421356f) continue;  // not a macro
        FP m = run(nd, bin_w, bin_h);
        if (m.weight != 1.0f) pass_bad++;              // no smoothing for a macro: weight exactly 1
    }
    if (passthrough_seen < 100) { printf("FAIL [4] only %d macros -- lost passthrough coverage\n", passthrough_seen); ok = false; }
    if (pass_bad) { printf("FAIL [4] %d interior macros not bit-exact passthrough\n", pass_bad); ok = false; }

    printf("[1] spec       max_rel=%.3e (tol %.0e)   [%d clamp-firing cells]\n", spec_max_rel, SPEC_TOL, clamp_fired);
    printf("[2] area       max_rel=%.3e (tol %.0e)\n", area_max_rel, AREA_TOL);
    printf("[3] on-grid    %d/%d footprints off-grid (incl. 5 edge-pinned cells)\n", off_grid, (int)cases.size());
    printf("[4] passthru   %d interior macros, %d with weight != 1.0f\n", passthrough_seen, pass_bad);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
