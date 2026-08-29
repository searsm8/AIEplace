// density_bin_model.cpp -- tier-1 coverage of the REAL density_bin module (Stage 1 binning).
//
// Proves the strip-tiled two-pass scatter the PL kernel actually runs (modules/density_bin.hpp,
// density_bin()) reproduces a naive full-grid scatter, BIT-EXACT. Each bin lives in exactly one
// strip and is accumulated from the same node set in the same order (fixed [M,N) -> per-bin cap
// min(rho,td) -> movable [0,M)) in both, so any off-by-one in the strip x-clipping or the
// rectangle intersection breaks the match.
//
// Before TODO #20 step 3 this file kept its OWN hand-copy of node_footprint / NodeBox / GRID
// because a pure-g++ TU could not include formats.hpp (its HLS transport headers). The wall is
// gone (formats.hpp guards those behind PL_TIER1_STUB): this now #includes the real module via
// tier1_stub.hpp and the naive reference shares the SAME node_footprint the module uses, so the
// test isolates the strip decomposition -- the geometry itself is covered by node_footprint_test.
//
// This is the algorithm/decomposition check. The sw_emu gate verifies density_bin against the
// actual Grid golden (Grid::computeBinOverlaps + clampFixedDensity) on a real benchmark. Note the
// PL node_footprint has two KNOWN divergences from that Grid golden -- the in-die shift and the
// movable-macro weight override -- both TODO #20 step 4; see node_footprint_test.cpp.

#include "tier1_stub.hpp"                 // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "modules/density_bin.hpp"        // the real module: density_bin(), STRIP, node_footprint

#include <vector>
#include <cstdio>
#include <cmath>
#include <random>
#include <algorithm>

using namespace plalgo;                   // NodeBox, GRID, STRIP, node_footprint, density_bin

// Naive reference: full GRID x GRID overlap accumulator, two-pass + per-bin cap, using the SAME
// node_footprint geometry as the module (so this checks the strip decomposition, not the footprint).
static std::vector<float> bin_reference(const std::vector<NodeBox>& nodes, int M,
                                        float bin_w, float bin_h, float target_density,
                                        int& clamped_bins) {
    const float bin_area = bin_w * bin_h;
    std::vector<float> ov(GRID * GRID, 0.0f);

    // One node's exact-rectangle scatter over the full grid (mirrors bin_scatter with c0=0,
    // strip=GRID -- i.e. no column clipping).
    auto scatter = [&](const NodeBox& nd) {
        float xl, yl, xh, yh, weight;
        node_footprint(nd, bin_w, bin_h, xl, yl, xh, yh, weight);
        int col_lo = (int)(xl / bin_w); if (col_lo < 0)        col_lo = 0;
        int col_hi = (int)(xh / bin_w); if (col_hi > GRID - 1) col_hi = GRID - 1;
        int row_lo = (int)(yl / bin_h); if (row_lo < 0)        row_lo = 0;
        int row_hi = (int)(yh / bin_h); if (row_hi > GRID - 1) row_hi = GRID - 1;
        for (int col = col_lo; col <= col_hi; col++) {
            const float lx = col * bin_w, rx = lx + bin_w;
            const float ox = (xh < rx ? xh : rx) - (xl > lx ? xl : lx);
            if (ox <= 0) continue;
            for (int row = row_lo; row <= row_hi; row++) {
                const float ly = row * bin_h, ry = ly + bin_h;
                const float oy = (yh < ry ? yh : ry) - (yl > ly ? yl : ly);
                if (oy <= 0) continue;
                ov[col * GRID + row] += ox * oy * weight;
            }
        }
    };

    for (int n = M; n < (int)nodes.size(); n++) scatter(nodes[n]);   // PASS 1: fixed [M,N)
    const float cap = bin_area * target_density;                     // min(rho,td) cap (TODO #35)
    clamped_bins = 0;
    for (float& v : ov) if (v > cap) { v = cap; clamped_bins++; }
    for (int n = 0; n < M; n++) scatter(nodes[n]);                   // PASS 2: movable [0,M)
    const float inv_area = 1.0f / bin_area;                          // multiply, matching the
    for (float& v : ov) v *= inv_area;                               // module (rho); x*(1/a) != x/a
    return ov;
}

int main() {
    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> upos(0.0f, 1.0f);

    const float die = 10000.0f;
    const float bin_w = die / GRID, bin_h = die / GRID;   // ~9.77
    const float target_density = 0.9f;

    std::vector<NodeBox> nodes;
    const int M = 8000;                                   // movable [0,M): mostly sub-bin std cells
    std::uniform_real_distribution<float> wsmall(2.0f, 30.0f);
    for (int i = 0; i < M; i++) {
        float w = wsmall(rng), h = wsmall(rng);
        nodes.push_back({ upos(rng) * (die - w), upos(rng) * (die - h), w, h });
    }
    const int Nfixed = 40;                                // fixed [M,N): macros -> exercise the cap
    std::uniform_real_distribution<float> wbig(100.0f, 1500.0f);
    for (int i = 0; i < Nfixed; i++) {
        float w = wbig(rng), h = wbig(rng);
        nodes.push_back({ upos(rng) * (die - w), upos(rng) * (die - h), w, h });
    }
    const int N = M + Nfixed;

    int clamped = 0;
    std::vector<float> ref = bin_reference(nodes, M, bin_w, bin_h, target_density, clamped);

    std::vector<float> rho(GRID * GRID, -1.0f);           // poison; density_bin must write every bin
    density_bin(nodes.data(), rho.data(), M, N, bin_w, bin_h, target_density);

    double max_abs = 0, sum_ref = 0;
    for (int i = 0; i < GRID * GRID; i++) {
        max_abs = std::max(max_abs, (double)std::fabs(rho[i] - ref[i]));
        sum_ref += ref[i];
    }

    printf("[info] GRID=%d STRIP=%d  movable=%d fixed=%d  bin=%.4fx%.4f td=%.2f\n",
           GRID, STRIP, M, Nfixed, bin_w, bin_h, target_density);
    printf("[info] fixed-capped bins (rho_fixed >= td) = %d  total density mass = %.6g\n",
           clamped, sum_ref);
    printf("[1] strip-tiled vs naive scatter  max_abs_diff = %.3e (tol bit-exact)\n", max_abs);

    bool ok = true;
    // Bit-exact: same nodes, same order, same footprint into each single-strip bin.
    if (!(max_abs == 0.0)) { printf("FAIL [1] not bit-exact (max_abs=%.3e)\n", max_abs); ok = false; }
    // Guard the design still exercises the cap; a cap that silently stops firing must fail here.
    if (clamped < 100) { printf("FAIL [2] only %d capped bins -- design lost cap coverage\n", clamped); ok = false; }

    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
