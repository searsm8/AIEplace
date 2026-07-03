// ForceVerify.cpp -- see ForceVerify.hpp.
//
// Old-ABI TU (like FieldVerify): the golden gather is computed here in double; the new-ABI
// XRT Driver is reached only through runForceGather's POD + const char* boundary. The golden
// replicates force_gather's overlap-area-weighted field sum, which mirrors bin_scatter's
// rectangle-intersection geometry (verified bit-exact vs a naive scatter in Stage 1).

#include "ForceVerify.hpp"
#include "Driver.hpp"
#include "host_interface.hpp"

#include <vector>
#include <cmath>
#include <cstdio>
#include <random>

namespace plalgo {

// Golden gather for one node: sum over overlapped bins of overlap_area * field (double).
static void node_gather_ref(const NodeBox& nd, const std::vector<float>& efx,
                            const std::vector<float>& efy, float bin_w, float bin_h,
                            double& grad_x, double& grad_y) {
    const int G = DENSITY_GRID;
    const double xl = nd.x, yl = nd.y, xh = nd.x + nd.w, yh = nd.y + nd.h;
    int col_lo = (int)(xl / bin_w);  if (col_lo < 0)     col_lo = 0;
    int col_hi = (int)(xh / bin_w);  if (col_hi > G - 1) col_hi = G - 1;
    int row_lo = (int)(yl / bin_h);  if (row_lo < 0)     row_lo = 0;
    int row_hi = (int)(yh / bin_h);  if (row_hi > G - 1) row_hi = G - 1;

    double ax = 0, ay = 0;
    if (col_lo == col_hi && row_lo == row_hi) {
        const size_t idx = (size_t)col_lo * G + row_lo;
        const double area = (double)nd.w * nd.h;
        ax = area * efx[idx];
        ay = area * efy[idx];
    } else {
        for (int col = col_lo; col <= col_hi; col++) {
            const double lx = col * (double)bin_w, rx = lx + bin_w;
            const double ox = (xh < rx ? xh : rx) - (xl > lx ? xl : lx);
            if (ox <= 0) continue;
            for (int row = row_lo; row <= row_hi; row++) {
                const double ly = row * (double)bin_h, ry = ly + bin_h;
                const double oy = (yh < ry ? yh : ry) - (yl > ly ? yl : ly);
                if (oy <= 0) continue;
                const double area = ox * oy;
                const size_t idx = (size_t)col * G + row;
                ax += area * efx[idx];
                ay += area * efy[idx];
            }
        }
    }
    grad_x = ax;
    grad_y = ay;
}

int runForceGatherVerify(const char* xclbin_path) {
    const int   G     = DENSITY_GRID;   // 1024
    const int   M     = 5000;           // movable nodes (all movable in this isolated test)
    const float bin_w = 1.0f, bin_h = 1.0f;   // unit bins -> die = G x G

    std::mt19937 rng(41);
    std::uniform_real_distribution<float> pos(0.0f, (float)(G - 16));   // keep bbox in-grid
    std::uniform_real_distribution<float> siz(0.3f, 12.0f);             // mix single-/multi-bin
    std::uniform_real_distribution<float> fld(-1.0f, 1.0f);

    std::vector<NodeBox> node_box(M);
    for (auto& nd : node_box) { nd.x = pos(rng); nd.y = pos(rng); nd.w = siz(rng); nd.h = siz(rng); }

    std::vector<float> efx((size_t)G * G), efy((size_t)G * G);
    for (auto& v : efx) v = fld(rng);
    for (auto& v : efy) v = fld(rng);

    printf("[force] verify: G=%d nodes=%d bin=%.1fx%.1f (gather sum_bins overlap_area*eField)\n",
           G, M, bin_w, bin_h);

    std::vector<coord_t> dev(M);
    runForceGather(node_box.data(), M, M, efx.data(), efy.data(), bin_w, bin_h,
                   dev.data(), xclbin_path);

    double sse = 0, ref = 0, max_abs = 0;
    for (int n = 0; n < M; n++) {
        double gx, gy;
        node_gather_ref(node_box[n], efx, efy, bin_w, bin_h, gx, gy);
        const double dx = (double)dev[n].x - gx, dy = (double)dev[n].y - gy;
        sse += dx * dx + dy * dy;  ref += gx * gx + gy * gy;
        if (std::fabs(dx) > max_abs) max_abs = std::fabs(dx);
        if (std::fabs(dy) > max_abs) max_abs = std::fabs(dy);
    }
    const double rr = std::sqrt(sse / (ref + 1e-30));
    const bool   ok = rr < 1e-3;
    printf("[force] gather vs overlap_area*eField golden: max_abs=%.3e  rel_rms=%.3e  -> %s\n",
           max_abs, rr, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace plalgo
