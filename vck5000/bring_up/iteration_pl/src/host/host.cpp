// host.cpp -- XRT host for iteration_pl: runs ONE full pl_algo Nesterov placement step
// (../pl/top.cpp: hpwl_CU -> density_bin -> field_solve_pl -> force_gather ->
// iteration_update/memory_writer) on real hardware and checks the stepped positions
// (u_{k+1}, v_{k+1}) against a from-scratch double-precision golden that mirrors each
// module's math exactly:
//   - HPWL: the same exact-exp full-WA gradient as ../../hpwl_pl/src/host/host.cpp
//     (== vck5000/host/src/pl_algo/src/HpwlGradVerify.cpp gradientGolden()).
//   - density scatter / force gather: node_footprint.hpp's sqrt(2) sub-bin clamp geometry,
//     replicated exactly (NOT the simplified unclamped rectangle ForceVerify.cpp's own
//     golden uses -- this harness's synthetic cells are small enough that the clamp
//     matters, so it has to be modeled to get a tight comparison).
//   - field solve: the same naive double 2D DCT/spectral/inverse as
//     vck5000/host/src/pl_algo/src/ForceVerify.cpp golden_field().
//   - combine + step + clamp: the same math as
//     vck5000/host/src/pl_algo/src/IterVerify.cpp runIterUpdateVerify().
//
// The dominant error source is hpwl_CU's LUT approximation of exp() (same as ../../hpwl_pl),
// so the tolerance (rel_rms < 3e-2 on the combined [u_out;v_out] state) is the same order as
// that harness's, not a new bound.
//
//   usage: ./host <xclbin>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"
#include "experimental/xrt_system.h"

#include "host_interface.hpp"

using plalgo::coord_t;
using plalgo::NodeBox;
using plalgo::NodePin;
using plalgo::NetBBox;
using plalgo::NetSums;

#ifndef PL_GRID
#define PL_GRID 64
#endif
static constexpr int GRID = PL_GRID;   // must match top.cpp's PL_GRID

// ---- synthetic design size (small: on-chip node_pos/gradient scratch is compile-time
// bounded by ITER_MAX_NODES in top.cpp, default 128) ----
static constexpr int M          = 48;   // movable nodes
static constexpr int NUM_FIXED  = 8;    // fixed (macro) nodes
static constexpr int N          = M + NUM_FIXED;
static constexpr int NUM_NETS   = 30;   // includes ~1/9 degree-1 "no gradient" nets

static const double PI = 3.14159265358979323846;

// ---- HPWL LUT: identical to ../../hpwl_pl/src/host/host.cpp ----
static constexpr float STEP_NORM  = 0.05f;
static constexpr int   GAMMA_MULT = 12;

static std::vector<float> buildExpLut(int& lut_size) {
    lut_size = (int)(GAMMA_MULT / STEP_NORM) + 2;
    std::vector<float> lut(lut_size);
    for (int i = 0; i < lut_size; i++) lut[i] = std::exp(-(float)i * STEP_NORM);
    return lut;
}

// Exact-exp full-WA gradient golden -- identical to ../../hpwl_pl/src/host/host.cpp.
static std::vector<coord_t> gradientGolden(const std::vector<coord_t>& node_pos,
                                           const std::vector<int32_t>& net_ptr,
                                           const std::vector<NodePin>& pins,
                                           float inv_gamma) {
    std::vector<coord_t> grad(M, coord_t{0.0f, 0.0f});
    for (int n = 0; n < NUM_NETS; n++) {
        const int beg = net_ptr[n], end = net_ptr[n + 1];
        const int deg = end - beg;
        if (deg <= 1) continue;
        float maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
        for (int p = beg; p < end; p++) {
            const NodePin& r = pins[p];
            const float x = node_pos[r.node_idx].x + r.off_x;
            const float y = node_pos[r.node_idx].y + r.off_y;
            maxx = std::max(maxx, x); minx = std::min(minx, x);
            maxy = std::max(maxy, y); miny = std::min(miny, y);
        }
        float Bpx=0,Bmx=0,Cpx=0,Cmx=0,Bpy=0,Bmy=0,Cpy=0,Cmy=0;
        for (int p = beg; p < end; p++) {
            const NodePin& r = pins[p];
            const float x = node_pos[r.node_idx].x + r.off_x;
            const float y = node_pos[r.node_idx].y + r.off_y;
            const float apx = std::exp((x - maxx) * inv_gamma);
            const float amx = std::exp((minx - x) * inv_gamma);
            const float apy = std::exp((y - maxy) * inv_gamma);
            const float amy = std::exp((miny - y) * inv_gamma);
            Bpx+=apx; Bmx+=amx; Cpx+=apx*x; Cmx+=amx*x;
            Bpy+=apy; Bmy+=amy; Cpy+=apy*y; Cmy+=amy*y;
        }
        const float bpx2=1.0f/(Bpx*Bpx), bmx2=1.0f/(Bmx*Bmx);
        const float bpy2=1.0f/(Bpy*Bpy), bmy2=1.0f/(Bmy*Bmy);
        for (int p = beg; p < end; p++) {
            const NodePin& r = pins[p];
            if (r.node_idx >= M) continue;
            const float x = node_pos[r.node_idx].x + r.off_x;
            const float y = node_pos[r.node_idx].y + r.off_y;
            const float apx = std::exp((x - maxx) * inv_gamma);
            const float amx = std::exp((minx - x) * inv_gamma);
            const float apy = std::exp((y - maxy) * inv_gamma);
            const float amy = std::exp((miny - y) * inv_gamma);
            const float px = ((1.0f + x*inv_gamma)*Bpx - Cpx*inv_gamma)*(apx*bpx2)
                           - ((1.0f - x*inv_gamma)*Bmx + Cmx*inv_gamma)*(amx*bmx2);
            const float py = ((1.0f + y*inv_gamma)*Bpy - Cpy*inv_gamma)*(apy*bpy2)
                           - ((1.0f - y*inv_gamma)*Bmy + Cmy*inv_gamma)*(amy*bmy2);
            grad[r.node_idx].x += px;
            grad[r.node_idx].y += py;
        }
    }
    return grad;
}

// ---- node_footprint.hpp, replicated exactly in double (see that file for the derivation:
// sub-bin cells are inflated to sqrt(2) bins/dimension and weighted so total area is
// conserved -- density_bin's scatter and force_gather's gather both depend on this being
// the SAME geometry on both sides, so the golden has to model it, not just the raw rect). ----
static void nodeFootprintRef(double x, double y, double w, double h, double bin_w, double bin_h,
                             double& xl, double& yl, double& xh, double& yh, double& weight) {
    const double SQRT2 = 1.4142135623730951;
    const double min_w = bin_w * SQRT2, min_h = bin_h * SQRT2;
    const double cw = std::max(w, min_w), ch = std::max(h, min_h);
    weight = (cw > 0.0 && ch > 0.0) ? (w * h) / (cw * ch) : 0.0;
    const double grid_w = GRID * bin_w, grid_h = GRID * bin_h;
    xl = x + 0.5 * w - 0.5 * cw;
    yl = y + 0.5 * h - 0.5 * ch;
    if (xl + cw > grid_w) xl = grid_w - cw;
    if (yl + ch > grid_h) yl = grid_h - ch;
    if (xl < 0.0) xl = 0.0;
    if (yl < 0.0) yl = 0.0;
    xh = xl + cw;
    yh = yl + ch;
}

// Scatter one node's footprint (area-conserving) into a raw-area accumulator (NOT yet
// divided by bin_area) -- mirrors density_bin.hpp's bin_scatter exactly.
static void scatterFootprint(const NodeBox& nd, double bin_w, double bin_h,
                             std::vector<double>& acc) {
    double xl, yl, xh, yh, weight;
    nodeFootprintRef(nd.x, nd.y, nd.w, nd.h, bin_w, bin_h, xl, yl, xh, yh, weight);
    int col_lo = (int)(xl / bin_w);  if (col_lo < 0)        col_lo = 0;
    int col_hi = (int)(xh / bin_w);  if (col_hi > GRID - 1) col_hi = GRID - 1;
    int row_lo = (int)(yl / bin_h);  if (row_lo < 0)        row_lo = 0;
    int row_hi = (int)(yh / bin_h);  if (row_hi > GRID - 1) row_hi = GRID - 1;
    for (int col = col_lo; col <= col_hi; col++) {
        const double lx = col * bin_w, rx = lx + bin_w;
        const double ox = std::min(xh, rx) - std::max(xl, lx);
        if (ox <= 0) continue;
        for (int row = row_lo; row <= row_hi; row++) {
            const double ly = row * bin_h, ry = ly + bin_h;
            const double oy = std::min(yh, ry) - std::max(yl, ly);
            if (oy <= 0) continue;
            acc[(size_t)col * GRID + row] += ox * oy * weight;
        }
    }
}

// rho golden: fixed nodes scattered + clamped, then movable scattered on top, then
// divided by bin_area -- exactly density_bin.hpp's two-pass algorithm (TODO #35 CAP).
static std::vector<double> densityGolden(const std::vector<NodeBox>& node_box,
                                         double bin_w, double bin_h, double target_density) {
    std::vector<double> rho((size_t)GRID * GRID, 0.0);
    for (int n = M; n < N; n++) scatterFootprint(node_box[n], bin_w, bin_h, rho);
    const double cap = bin_w * bin_h * target_density;
    for (auto& v : rho) v = std::min(v, cap);
    for (int n = 0; n < M; n++) scatterFootprint(node_box[n], bin_w, bin_h, rho);
    const double inv_area = 1.0 / (bin_w * bin_h);
    for (auto& v : rho) v *= inv_area;
    return rho;
}

// Gather one node's force via the SAME footprint geometry -- exactly force_gather.hpp's
// node_gather.
static void nodeGatherRef(const NodeBox& nd, const std::vector<double>& efx,
                          const std::vector<double>& efy, double bin_w, double bin_h,
                          double& grad_x, double& grad_y) {
    double xl, yl, xh, yh, weight;
    nodeFootprintRef(nd.x, nd.y, nd.w, nd.h, bin_w, bin_h, xl, yl, xh, yh, weight);
    int col_lo = (int)(xl / bin_w);  if (col_lo < 0)        col_lo = 0;
    int col_hi = (int)(xh / bin_w);  if (col_hi > GRID - 1) col_hi = GRID - 1;
    int row_lo = (int)(yl / bin_h);  if (row_lo < 0)        row_lo = 0;
    int row_hi = (int)(yh / bin_h);  if (row_hi > GRID - 1) row_hi = GRID - 1;
    double ax = 0, ay = 0;
    for (int col = col_lo; col <= col_hi; col++) {
        const double lx = col * bin_w, rx = lx + bin_w;
        const double ox = std::min(xh, rx) - std::max(xl, lx);
        if (ox <= 0) continue;
        for (int row = row_lo; row <= row_hi; row++) {
            const double ly = row * bin_h, ry = ly + bin_h;
            const double oy = std::min(yh, ry) - std::max(yl, ly);
            if (oy <= 0) continue;
            const double area = ox * oy * weight;
            const size_t idx  = (size_t)col * GRID + row;
            ax += area * efx[idx];
            ay += area * efy[idx];
        }
    }
    grad_x = ax; grad_y = ay;
}

// ---- naive double 2D DCT/spectral/inverse field solve -- identical to
// vck5000/host/src/pl_algo/src/ForceVerify.cpp golden_field(). ----
static std::vector<double> make_Cdct(int Nn) {
    std::vector<double> C((size_t)Nn * Nn);
    for (int k = 0; k < Nn; k++) for (int n = 0; n < Nn; n++)
        C[(size_t)k * Nn + n] = std::cos(PI / Nn * (n + 0.5) * k);
    return C;
}
static std::vector<double> make_Cidct(int Nn) {
    std::vector<double> C((size_t)Nn * Nn);
    for (int k = 0; k < Nn; k++) for (int n = 0; n < Nn; n++)
        C[(size_t)k * Nn + n] = (n == 0) ? 0.5 : std::cos(PI / Nn * (k + 0.5) * n);
    return C;
}
static void matvec(const double* basis, const double* x, double* out, int Nn) {
    for (int k = 0; k < Nn; k++) {
        const double* b = &basis[(size_t)k * Nn]; double s = 0;
        for (int n = 0; n < Nn; n++) s += b[n] * x[n];
        out[k] = s;
    }
}
static void transposeSq(const std::vector<double>& A, std::vector<double>& T, int Nn) {
    for (int i = 0; i < Nn; i++) for (int j = 0; j < Nn; j++)
        T[(size_t)j * Nn + i] = A[(size_t)i * Nn + j];
}
static void dct_rows(const std::vector<double>& A, std::vector<double>& out,
                     const std::vector<double>& C, int Nn) {
    for (int r = 0; r < Nn; r++) matvec(C.data(), &A[(size_t)r * Nn], &out[(size_t)r * Nn], Nn);
}
static void idxst_rows(const std::vector<double>& A, std::vector<double>& out,
                       const std::vector<double>& Cidct, int Nn) {
    std::vector<double> t(Nn);
    for (int r = 0; r < Nn; r++) {
        const double* x = &A[(size_t)r * Nn];
        t[0] = x[0];
        for (int n = 1; n < Nn; n++) t[n] = x[Nn - n];
        double* o = &out[(size_t)r * Nn];
        matvec(Cidct.data(), t.data(), o, Nn);
        for (int k = 1; k < Nn; k += 2) o[k] = -o[k];
    }
}
static void golden_field(const std::vector<double>& rho, std::vector<double>& Ex,
                         std::vector<double>& Ey, int Nn) {
    const std::vector<double> Cdct = make_Cdct(Nn), Cidct = make_Cidct(Nn);
    std::vector<double> A((size_t)Nn*Nn), B((size_t)Nn*Nn), a_uv((size_t)Nn*Nn),
                        Ex_hat((size_t)Nn*Nn), Ey_hat((size_t)Nn*Nn);
    dct_rows(rho, B, Cdct, Nn); transposeSq(B, A, Nn);
    dct_rows(A, B, Cdct, Nn);   transposeSq(B, a_uv, Nn);
    const double TWO_PI = 2 * PI;
    for (int u = 0; u < Nn; u++) {
        const double w_u = TWO_PI * u / Nn;
        for (int v = 0; v < Nn; v++) {
            const double w_v = TWO_PI * v / Nn; const size_t i = (size_t)u * Nn + v;
            if (u == 0 && v == 0) { Ex_hat[i] = 0; Ey_hat[i] = 0; continue; }
            const double denom = w_u*w_u + w_v*w_v;
            Ex_hat[i] = a_uv[i] * w_u / denom;  Ey_hat[i] = a_uv[i] * w_v / denom;
        }
    }
    Ex.assign((size_t)Nn*Nn, 0); Ey.assign((size_t)Nn*Nn, 0);
    dct_rows(Ex_hat, A, Cidct, Nn); transposeSq(A, B, Nn); idxst_rows(B, A, Cidct, Nn); transposeSq(A, Ex, Nn);
    idxst_rows(Ey_hat, A, Cidct, Nn); transposeSq(A, B, Nn); dct_rows(B, A, Cidct, Nn); transposeSq(A, Ey, Nn);
}

// ---- combine + BB step + Nesterov momentum + die clamp -- identical math to
// vck5000/host/src/pl_algo/src/IterVerify.cpp runIterUpdateVerify(). ----
static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <xclbin>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* xclbin_path = argv[1];

    // ---- synthetic design: die = GRID x GRID with unit bins (bin_w=bin_h=1.0). ----
    const double die = (double)GRID;
    const double bin_w = die / GRID, bin_h = die / GRID;   // = 1.0
    const double target_density = 0.9;

    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> mov_pos(2.0f, (float)GRID - 4.0f);
    std::uniform_real_distribution<float> mov_siz(0.4f, 1.8f);     // mostly sub-bin -> exercises the clamp
    std::uniform_real_distribution<float> fix_pos(4.0f, (float)GRID - 12.0f);
    std::uniform_real_distribution<float> fix_siz(4.0f, 8.0f);     // macro-scale, well above the clamp
    std::uniform_real_distribution<float> jit(-1.0f, 1.0f);        // u_k jitter around v_k
    std::uniform_real_distribution<float> off_uni(-0.5f, 0.5f);
    std::uniform_real_distribution<float> off_prob(0.0f, 1.0f);
    std::uniform_int_distribution<int>    node_uni(0, N - 1);
    std::uniform_int_distribution<int>    deg_uni(2, 6);

    std::vector<NodeBox> node_box(N);
    for (int n = 0; n < M; n++) {
        node_box[n].w = mov_siz(rng); node_box[n].h = mov_siz(rng);
        node_box[n].x = mov_pos(rng); node_box[n].y = mov_pos(rng);
    }
    for (int n = M; n < N; n++) {
        node_box[n].w = fix_siz(rng); node_box[n].h = fix_siz(rng);
        node_box[n].x = fix_pos(rng); node_box[n].y = fix_pos(rng);
    }

    std::vector<coord_t> node_pos(N);
    for (int n = 0; n < N; n++) { node_pos[n].x = node_box[n].x; node_pos[n].y = node_box[n].y; }

    std::vector<coord_t> u_in(M);
    for (int n = 0; n < M; n++) {
        u_in[n].x = node_box[n].x + jit(rng);
        u_in[n].y = node_box[n].y + jit(rng);
    }
    std::vector<float> precond(M, 1.0f);   // v1 default: preconditioner OFF

    // ---- HPWL netlist: same shape as ../../hpwl_pl (random-degree nets, ~1/9 masked) ----
    std::vector<int32_t> net_ptr(NUM_NETS + 1, 0);
    std::vector<NodePin> pins;
    pins.reserve((size_t)NUM_NETS * 5);
    for (int net = 0; net < NUM_NETS; net++) {
        const bool masked = (net % 9 == 0);
        const int  deg    = masked ? 1 : deg_uni(rng);
        net_ptr[net] = (int32_t)pins.size();
        for (int k = 0; k < deg; k++) {
            NodePin r;
            r.node_idx = node_uni(rng);
            const bool has_offset = off_prob(rng) < 0.15f;
            r.off_x = has_offset ? off_uni(rng) : 0.0f;
            r.off_y = has_offset ? off_uni(rng) : 0.0f;
            r.net   = masked ? -1 : net;
            pins.push_back(r);
        }
    }
    net_ptr[NUM_NETS] = (int32_t)pins.size();

    std::vector<NodePin> npins;
    npins.reserve(pins.size());
    for (const NodePin& r : pins)
        if (r.net >= 0 && r.node_idx < M) npins.push_back(r);
    std::stable_sort(npins.begin(), npins.end(),
                     [](const NodePin& a, const NodePin& b){ return a.node_idx < b.node_idx; });
    const int num_npins = (int)npins.size();

    float maxx=-1e30f,minx=1e30f,maxy=-1e30f,miny=1e30f;
    for (const coord_t& c : node_pos) {
        maxx=std::max(maxx,c.x); minx=std::min(minx,c.x);
        maxy=std::max(maxy,c.y); miny=std::min(miny,c.y);
    }
    const float span = std::max(maxx-minx, maxy-miny);
    const float gamma = 0.01f * span;
    const float inv_gamma = 1.0f / gamma;
    const float inv_lut_step = 1.0f / (STEP_NORM * gamma);
    int lut_size;
    std::vector<float> lut = buildExpLut(lut_size);

    // ---- iteration_update scalars ----
    const float lambda = 0.3f, alpha = 0.05f, coeff = 0.2f;
    const float die_xmax = (float)die, die_ymax = (float)die;

    std::printf("[iteration_pl] M=%d N=%d nets=%d npins=%d GRID=%d  span=%.3g gamma=%.3g lut=%d\n",
               M, N, NUM_NETS, num_npins, GRID, span, gamma, lut_size);

    // ---- device / xclbin (probe all enumerated devices; see ../../hpwl_pl) ----
    xrt::device device;
    xrt::uuid   uuid;
    bool        loaded = false;
    unsigned int ndev = xrt::system::enumerate_devices();
    for (unsigned int i = 0; i < ndev; i++) {
        try {
            xrt::device d = xrt::device(i);
            uuid = d.load_xclbin(xclbin_path);
            device = d;
            loaded = true;
            break;
        } catch (const std::exception&) {
            continue;
        }
    }
    if (!loaded) {
        std::printf("failed to load %s on any of %u device(s)\n", xclbin_path, ndev);
        return EXIT_FAILURE;
    }
    xrt::kernel k = xrt::kernel(device, uuid, "iteration_top");

    // ---- buffers: net_ptr(0) pins(1) npins(2) exp_lut(3) bb(4) sums(5) node_box(6)
    // u_in(7) precond(8) u_out(9) v_out(10) ----
    const size_t nptr_bytes  = net_ptr.size()  * sizeof(int32_t);
    const size_t pins_bytes  = pins.size()     * sizeof(NodePin);
    const size_t npins_bytes = npins.size()    * sizeof(NodePin);
    const size_t lut_bytes   = (size_t)lut_size * sizeof(float);
    const size_t bb_bytes    = (size_t)NUM_NETS * sizeof(NetBBox);
    const size_t sums_bytes  = (size_t)NUM_NETS * sizeof(NetSums);
    const size_t box_bytes   = node_box.size() * sizeof(NodeBox);
    const size_t coordM      = (size_t)M * sizeof(coord_t);
    const size_t precondB    = (size_t)M * sizeof(float);

    xrt::bo bo_nptr  = xrt::bo(device, nptr_bytes,  k.group_id(0));
    xrt::bo bo_pins  = xrt::bo(device, pins_bytes,  k.group_id(1));
    xrt::bo bo_npins = xrt::bo(device, npins_bytes, k.group_id(2));
    xrt::bo bo_lut   = xrt::bo(device, lut_bytes,   k.group_id(3));
    xrt::bo bo_bb    = xrt::bo(device, bb_bytes,    k.group_id(4));
    xrt::bo bo_sums  = xrt::bo(device, sums_bytes,  k.group_id(5));
    xrt::bo bo_box   = xrt::bo(device, box_bytes,   k.group_id(6));
    xrt::bo bo_uin   = xrt::bo(device, coordM,      k.group_id(7));
    xrt::bo bo_prec  = xrt::bo(device, precondB,    k.group_id(8));
    xrt::bo bo_uout  = xrt::bo(device, coordM,      k.group_id(9));
    xrt::bo bo_vout  = xrt::bo(device, coordM,      k.group_id(10));

    std::memcpy(bo_nptr.map<void*>(),  net_ptr.data(),  nptr_bytes);
    std::memcpy(bo_pins.map<void*>(),  pins.data(),     pins_bytes);
    std::memcpy(bo_npins.map<void*>(), npins.data(),    npins_bytes);
    std::memcpy(bo_lut.map<void*>(),   lut.data(),      lut_bytes);
    std::memcpy(bo_box.map<void*>(),   node_box.data(), box_bytes);
    std::memcpy(bo_uin.map<void*>(),   u_in.data(),     coordM);
    std::memcpy(bo_prec.map<void*>(),  precond.data(),  precondB);
    std::memset(bo_uout.map<void*>(), 0, coordM);
    std::memset(bo_vout.map<void*>(), 0, coordM);

    bo_nptr.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_pins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_npins.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_lut.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_box.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_uin.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_prec.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_uout.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_vout.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // ---- golden, computed BEFORE the device run (pure host math, no dependency on it) ----
    std::vector<coord_t> g_hpwl_gold = gradientGolden(node_pos, net_ptr, pins, inv_gamma);
    std::vector<double>  rho_gold    = densityGolden(node_box, bin_w, bin_h, target_density);
    std::vector<double>  Ex_gold, Ey_gold;
    golden_field(rho_gold, Ex_gold, Ey_gold, GRID);

    std::vector<double> u_out_gold(2 * M), v_out_gold(2 * M);  // interleaved {x,y} per node
    for (int n = 0; n < M; n++) {
        double gdx, gdy;
        nodeGatherRef(node_box[n], Ex_gold, Ey_gold, bin_w, bin_h, gdx, gdy);
        const double gx = (double)g_hpwl_gold[n].x - (double)lambda * gdx;
        const double gy = (double)g_hpwl_gold[n].y - (double)lambda * gdy;
        const double pgx = gx / precond[n], pgy = gy / precond[n];   // precond==1.0
        const double uxr = (double)node_box[n].x - (double)alpha * pgx;
        const double uyr = (double)node_box[n].y - (double)alpha * pgy;
        const double vxr = uxr + (double)coeff * (uxr - u_in[n].x);
        const double vyr = uyr + (double)coeff * (uyr - u_in[n].y);
        const double mx = die_xmax - node_box[n].w, my = die_ymax - node_box[n].h;
        u_out_gold[2*n]   = clampd(uxr, 0.0, mx); u_out_gold[2*n+1] = clampd(uyr, 0.0, my);
        v_out_gold[2*n]   = clampd(vxr, 0.0, mx); v_out_gold[2*n+1] = clampd(vyr, 0.0, my);
    }

    // ---- run on hardware ----
    xrt::run run = k(bo_nptr, bo_pins, bo_npins, bo_lut, bo_bb, bo_sums, bo_box,
                     bo_uin, bo_prec, bo_uout, bo_vout,
                     inv_gamma, inv_lut_step, lut_size, NUM_NETS, num_npins,
                     (float)bin_w, (float)bin_h, (float)target_density,
                     lambda, alpha, coeff, die_xmax, die_ymax,
                     M, N);
    run.wait();
    bo_uout.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    bo_vout.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    coord_t* u_out = bo_uout.map<coord_t*>();
    coord_t* v_out = bo_vout.map<coord_t*>();

    // ---- verify: relative RMS over the combined [u_out; v_out] state ----
    double sse = 0, ref2 = 0, max_abs = 0;
    for (int n = 0; n < M; n++) {
        const double dux = (double)u_out[n].x - u_out_gold[2*n],   duy = (double)u_out[n].y - u_out_gold[2*n+1];
        const double dvx = (double)v_out[n].x - v_out_gold[2*n],   dvy = (double)v_out[n].y - v_out_gold[2*n+1];
        sse += dux*dux + duy*duy + dvx*dvx + dvy*dvy;
        ref2 += u_out_gold[2*n]*u_out_gold[2*n] + u_out_gold[2*n+1]*u_out_gold[2*n+1]
              + v_out_gold[2*n]*v_out_gold[2*n] + v_out_gold[2*n+1]*v_out_gold[2*n+1];
        max_abs = std::max({max_abs, std::fabs(dux), std::fabs(duy), std::fabs(dvx), std::fabs(dvy)});
    }
    const double rel_rms = (ref2 > 0) ? std::sqrt(sse / ref2) : 0.0;
    const double TOL = 3e-2;   // dominated by hpwl_CU's LUT-vs-exact-exp error, same order as ../../hpwl_pl
    const bool   ok  = rel_rms < TOL;

    std::printf("max_abs=%.3e  rel_rms=%.3e\n", max_abs, rel_rms);
    std::printf("%s  M=%d  (rel_rms=%.3e, tol %.0e)  iteration_pl on VCK5000\n",
               ok ? "TEST PASSED" : "TEST FAILED", M, rel_rms, TOL);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
