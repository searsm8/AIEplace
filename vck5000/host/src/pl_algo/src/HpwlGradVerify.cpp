// HpwlGradVerify.cpp -- see HpwlGradVerify.hpp.

#include "HpwlGradVerify.hpp"
#include "Driver.hpp"
#include "host_interface.hpp"

#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace plalgo {

// Normalized exp LUT, matching the kernel's lookup: lut[i] = exp(-i*step_norm),
// index = d * inv_lut_step where inv_lut_step = 1/(step_norm*gamma), so
// lut(d) ~= exp(-d/gamma). Fine resolution here so the LUT error is small and a
// mismatch flags a kernel bug rather than LUT coarseness.
// TODO: More precision could be added if extra URAM is available.
static constexpr float STEP_NORM = 0.05f;  // step in normalized units (d/gamma)
static constexpr int   GAMMA_MULT = 12;    // table covers up to 12*gamma

static std::vector<float> buildExpLut(int& lut_size) {
    lut_size = (int)(GAMMA_MULT / STEP_NORM) + 2;
    std::vector<float> lut(lut_size);
    for (int i = 0; i < lut_size; i++) lut[i] = std::exp(-(float)i * STEP_NORM);
    return lut;
}

// Exact-exp full-WA gradient golden (mirrors markv1 computeHpwlPartials_CPU).
// Float math throughout to match the kernel, so the comparison isolates LUT-vs-exp.
static std::vector<coord_t> gradientGolden(const PackedDesign& pk, float inv_gamma) {
    const int M = pk.header.num_movable;
    std::vector<coord_t> grad(M, coord_t{0.0f, 0.0f});

    for (int n = 0; n < pk.header.num_nets; n++) {
        const int beg = pk.net_ptr[n], end = pk.net_ptr[n + 1];
        const int deg = end - beg;
        if (deg <= 1) continue;

        float maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
        for (int p = beg; p < end; p++) {
            const NodePin& r = pk.pins[p];
            const float x = pk.node_pos[r.node_idx].x + r.off_x;
            const float y = pk.node_pos[r.node_idx].y + r.off_y;
            maxx = std::max(maxx, x); minx = std::min(minx, x);
            maxy = std::max(maxy, y); miny = std::min(miny, y);
        }

        float Bpx = 0, Bmx = 0, Cpx = 0, Cmx = 0, Bpy = 0, Bmy = 0, Cpy = 0, Cmy = 0;
        for (int p = beg; p < end; p++) {
            const NodePin& r = pk.pins[p];
            const float x = pk.node_pos[r.node_idx].x + r.off_x;
            const float y = pk.node_pos[r.node_idx].y + r.off_y;
            const float apx = std::exp((x - maxx) * inv_gamma);
            const float amx = std::exp((minx - x) * inv_gamma);
            const float apy = std::exp((y - maxy) * inv_gamma);
            const float amy = std::exp((miny - y) * inv_gamma);
            Bpx += apx; Bmx += amx; Cpx += apx * x; Cmx += amx * x;
            Bpy += apy; Bmy += amy; Cpy += apy * y; Cmy += amy * y;
        }
        const float bpx2 = 1.0f / (Bpx * Bpx), bmx2 = 1.0f / (Bmx * Bmx);
        const float bpy2 = 1.0f / (Bpy * Bpy), bmy2 = 1.0f / (Bmy * Bmy);

        for (int p = beg; p < end; p++) {
            const NodePin& r = pk.pins[p];
            if (r.node_idx >= M) continue; // fixed: no stored gradient
            const float x = pk.node_pos[r.node_idx].x + r.off_x;
            const float y = pk.node_pos[r.node_idx].y + r.off_y;
            const float apx = std::exp((x - maxx) * inv_gamma);
            const float amx = std::exp((minx - x) * inv_gamma);
            const float apy = std::exp((y - maxy) * inv_gamma);
            const float amy = std::exp((miny - y) * inv_gamma);
            const float px = ((1.0f + x * inv_gamma) * Bpx - Cpx * inv_gamma) * (apx * bpx2)
                           - ((1.0f - x * inv_gamma) * Bmx + Cmx * inv_gamma) * (amx * bmx2);
            const float py = ((1.0f + y * inv_gamma) * Bpy - Cpy * inv_gamma) * (apy * bpy2)
                           - ((1.0f - y * inv_gamma) * Bmy + Cmy * inv_gamma) * (amy * bmy2);
            grad[r.node_idx].x += px;
            grad[r.node_idx].y += py;
        }
    }
    return grad;
}

int runHpwlGradVerify(const PackedDesign& pk, const char* xclbin_path) {
    const int M = pk.header.num_movable;

    // Pick gamma from the design's coordinate span (1% of the larger extent).
    float maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
    for (const coord_t& c : pk.node_pos) {
        maxx = std::max(maxx, c.x); minx = std::min(minx, c.x);
        maxy = std::max(maxy, c.y); miny = std::min(miny, c.y);
    }
    const float span = std::max(maxx - minx, maxy - miny);
    const float gamma = 0.01f * span;
    const float inv_gamma = 1.0f / gamma;
    const float inv_lut_step = 1.0f / (STEP_NORM * gamma);

    int lut_size;
    std::vector<float> lut = buildExpLut(lut_size);

    printf("[hpwl_grad] verify: M=%d nets=%d pins=%d  span=%.3g gamma=%.3g lut=%d\n",
           M, pk.header.num_nets, pk.header.num_pins, span, gamma, lut_size);

    // PL kernel.
    std::vector<coord_t> dev(M, coord_t{0.0f, 0.0f});
    runHpwlGradCU(pk, lut.data(), lut_size, inv_gamma, inv_lut_step, dev.data(), xclbin_path);

    // Golden.
    std::vector<coord_t> gold = gradientGolden(pk, inv_gamma);

    // Compare per movable node (magnitude-based relative error on non-tiny nodes).
    double max_abs = 0, max_rel = 0, sse = 0, sse_ref = 0;
    int counted = 0;
    for (int i = 0; i < M; i++) {
        const double ex = gold[i].x, ey = gold[i].y, ax = dev[i].x, ay = dev[i].y;
        const double dmag = std::sqrt((ax - ex) * (ax - ex) + (ay - ey) * (ay - ey));
        const double rmag = std::sqrt(ex * ex + ey * ey);
        max_abs = std::max(max_abs, dmag);
        sse += dmag * dmag; sse_ref += rmag * rmag;
        if (rmag > 1e-4) { max_rel = std::max(max_rel, dmag / rmag); counted++; }
    }
    const double rms      = std::sqrt(sse / std::max(M, 1));
    const double rel_rms  = (sse_ref > 0) ? std::sqrt(sse / sse_ref) : 0.0; // global ||err||/||ref||
    const double TOL      = 2e-2;
    const bool   pass     = (rel_rms < TOL);

    printf("[hpwl_grad] max_abs=%.3e  max_rel=%.3e (%d nodes)  rms=%.3e  rel_rms=%.3e  -> %s\n",
           max_abs, max_rel, counted, rms, rel_rms, pass ? "PASS" : "FAIL");
    for (int i = 0; i < std::min(M, 4); i++)
        printf("    node %d: dev=(%.6f,%.6f) gold=(%.6f,%.6f)\n",
               i, dev[i].x, dev[i].y, gold[i].x, gold[i].y);

    return pass ? 0 : 1;
}

} // namespace plalgo
