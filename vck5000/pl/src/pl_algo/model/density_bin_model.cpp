// density_bin_model.cpp -- pl_algo density-solve Stage 1 binning algorithm model.
//
// Proves, in pure host C++ (no HLS), that the STRIP-TILED two-pass binning the PL
// kernel will use reproduces a naive full-grid scatter: same exact rect-intersection
// as sw_only Grid::computeBinOverlaps (fast path for sub-bin cells, else exact rect
// area), same two-pass order (fixed -> clamp -> movable), fillers excluded. Each bin
// is in exactly one strip and is accumulated from the same node set in the same order
// in both versions, so the match should be BIT-EXACT; any off-by-one in the strip
// x-clipping or the intersection breaks it.
//
// The real module density_bin.hpp mirrors this algorithm; the sw_emu gate later
// verifies that module against the actual Grid golden on a real benchmark.
//
// Build + run (WSL):  g++ -O2 -std=c++17 density_bin_model.cpp -o density_bin_model && ./density_bin_model

#include <vector>
#include <cstdio>
#include <cmath>
#include <random>
#include <algorithm>

constexpr int GRID    = 1024;          // bins per row/col (formats.hpp)
constexpr int STRIP   = 64;            // x-values per strip; 1024/64 = 16 strips
constexpr int N_BINS  = GRID * GRID;

struct NodeBox { float x, y, w, h; };  // lower-left anchor + size (host_interface.hpp)

constexpr bool ENABLE_DENSITY_CLAMP = true;   // mirror node_footprint.hpp

// Clamped, area-conserving, on-grid footprint (mirrors modules/node_footprint.hpp).
static void node_footprint(const NodeBox& nd, float bin_w, float bin_h,
                           float& xl, float& yl, float& xh, float& yh, float& weight) {
    const float w = nd.w, h = nd.h;
    float cw = w, ch = h;
    weight = 1.0f;
    if (ENABLE_DENSITY_CLAMP) {
        const float SQRT2 = 1.41421356f;
        const float min_w = bin_w * SQRT2, min_h = bin_h * SQRT2;
        cw = std::max(w, min_w);
        ch = std::max(h, min_h);
        weight = (cw > 0.0f && ch > 0.0f) ? (w * h) / (cw * ch) : 0.0f;
    }
    const float grid_w = GRID * bin_w, grid_h = GRID * bin_h;
    xl = nd.x + 0.5f * w - 0.5f * cw;
    yl = nd.y + 0.5f * h - 0.5f * ch;
    if (xl + cw > grid_w) xl = grid_w - cw;
    if (yl + ch > grid_h) yl = grid_h - ch;
    if (xl < 0.0f) xl = 0.0f;
    if (yl < 0.0f) yl = 0.0f;
    xh = xl + cw;
    yh = yl + ch;
}

// Per-node scatter over the clamped footprint, restricted to columns (x-bins) in
// [clip_lo, clip_hi). add(col,row,area) accumulates one bin's area-conserving deposit.
template <class AddFn>
static void scatter(const NodeBox& nd, float bin_w, float bin_h,
                    int clip_lo, int clip_hi, AddFn add) {
    float xl, yl, xh, yh, weight;
    node_footprint(nd, bin_w, bin_h, xl, yl, xh, yh, weight);
    const int col_lo = std::max(0, (int)(xl / bin_w));
    const int col_hi = std::min(GRID - 1, (int)(xh / bin_w));
    const int row_lo = std::max(0, (int)(yl / bin_h));
    const int row_hi = std::min(GRID - 1, (int)(yh / bin_h));

    const int cs = std::max(col_lo, clip_lo), ce = std::min(col_hi, clip_hi - 1);
    for (int col = cs; col <= ce; col++) {
        const float ox = std::min(xh, (col + 1) * bin_w) - std::max(xl, col * bin_w);
        if (ox <= 0) continue;
        for (int row = row_lo; row <= row_hi; row++) {
            const float oy = std::min(yh, (row + 1) * bin_h) - std::max(yl, row * bin_h);
            if (oy <= 0) continue;
            add(col, row, ox * oy * weight);
        }
    }
}

// Naive reference: full GRID x GRID overlap accumulator, two-pass + clamp.
static std::vector<float> bin_reference(const std::vector<NodeBox>& nodes, int M,
                                        float bin_w, float bin_h, float target_density,
                                        int& clamped_bins) {
    const float bin_area = bin_w * bin_h;
    std::vector<float> ov(N_BINS, 0.0f);
    auto add = [&](int c, int r, float a){ ov[c * GRID + r] += a; };

    for (int n = M; n < (int)nodes.size(); n++)           // PASS 1: fixed [M,N)
        scatter(nodes[n], bin_w, bin_h, 0, GRID, add);
    clamped_bins = 0;
    const float cap = target_density * bin_area;          // CLAMP
    for (float& v : ov) { if (v > cap) { v = cap; clamped_bins++; } }
    for (int n = 0; n < M; n++)                           // PASS 2: movable [0,M)
        scatter(nodes[n], bin_w, bin_h, 0, GRID, add);

    for (float& v : ov) v /= bin_area;                    // rho
    return ov;
}

// Strip-tiled: 256 KB on-chip strip, nodes re-streamed per strip.
static std::vector<float> bin_strip_tiled(const std::vector<NodeBox>& nodes, int M,
                                          float bin_w, float bin_h, float target_density) {
    const float bin_area = bin_w * bin_h;
    const float cap = target_density * bin_area;
    std::vector<float> rho(N_BINS, 0.0f);
    std::vector<float> acc(STRIP * GRID);                 // the on-chip strip accumulator

    for (int c0 = 0; c0 < GRID; c0 += STRIP) {
        std::fill(acc.begin(), acc.end(), 0.0f);
        auto add = [&](int c, int r, float a){ acc[(c - c0) * GRID + r] += a; };
        for (int n = M; n < (int)nodes.size(); n++)       // PASS 1: fixed
            scatter(nodes[n], bin_w, bin_h, c0, c0 + STRIP, add);
        for (float& v : acc) if (v > cap) v = cap;        // CLAMP (strip-local; each bin in one strip)
        for (int n = 0; n < M; n++)                       // PASS 2: movable
            scatter(nodes[n], bin_w, bin_h, c0, c0 + STRIP, add);
        for (int i = 0; i < STRIP; i++)                   // write strip -> rho (x-major)
            for (int r = 0; r < GRID; r++)
                rho[(c0 + i) * GRID + r] = acc[i * GRID + r] / bin_area;
    }
    return rho;
}

int main() {
    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> upos(0.0f, 1.0f);

    const float die = 10000.0f;
    const float bin_w = die / GRID, bin_h = die / GRID;   // ~9.77
    const float target_density = 0.9f;

    std::vector<NodeBox> nodes;
    // Movable [0,M): mostly sub-bin std cells, some multi-bin.
    const int M = 8000;
    std::uniform_real_distribution<float> wsmall(2.0f, 30.0f);
    for (int i = 0; i < M; i++) {
        float w = wsmall(rng), h = wsmall(rng);
        nodes.push_back({ upos(rng) * (die - w), upos(rng) * (die - h), w, h });
    }
    // Fixed [M,N): big macros -> fully cover interior bins -> exercise the clamp.
    const int Nfixed = 40;
    std::uniform_real_distribution<float> wbig(100.0f, 1500.0f);
    for (int i = 0; i < Nfixed; i++) {
        float w = wbig(rng), h = wbig(rng);
        nodes.push_back({ upos(rng) * (die - w), upos(rng) * (die - h), w, h });
    }

    printf("== Stage 1: binning model (strip-tiled vs naive scatter) ==\n");
    printf("   GRID=%d STRIP=%d  movable=%d fixed=%d  bin=%.4fx%.4f td=%.2f\n",
           GRID, STRIP, M, Nfixed, bin_w, bin_h, target_density);

    int clamped = 0;
    std::vector<float> ref   = bin_reference(nodes, M, bin_w, bin_h, target_density, clamped);
    std::vector<float> strip = bin_strip_tiled(nodes, M, bin_w, bin_h, target_density);

    double max_abs = 0, sum_ref = 0;
    for (int i = 0; i < N_BINS; i++) {
        max_abs = std::max(max_abs, (double)std::fabs(strip[i] - ref[i]));
        sum_ref += ref[i];
    }
    printf("   clamped bins (fixed at cap) = %d\n", clamped);
    printf("   total density mass = %.6g   max_abs_diff(strip - ref) = %.3e\n", sum_ref, max_abs);
    printf("   -> %s\n", max_abs < 1e-12 ? "PASS (bit-exact)" : "FAIL");
    return max_abs < 1e-12 ? 0 : 1;
}
