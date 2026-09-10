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

// ---- naive golden field pipeline (double), mirroring compute_a_uv_DCT + compute_eField_DCT.
//      Same convention proven exact by the Stage 0 model; used here to build the golden Ex/Ey
//      from the golden rho. NxN row-major (matrix[p][q] = m[p*N+q]).
static const double PI = 3.14159265358979323846;

static std::vector<double> make_Cdct(int N) {   // DCT_naive basis: cos(pi/N*(n+0.5)*k)
    std::vector<double> C((size_t)N * N);
    for (int k = 0; k < N; k++) for (int n = 0; n < N; n++)
        C[(size_t)k * N + n] = std::cos(PI / N * (n + 0.5) * k);
    return C;
}
static std::vector<double> make_Cidct(int N) {  // IDCT_naive basis: (n==0)?0.5:cos(pi/N*(k+0.5)*n)
    std::vector<double> C((size_t)N * N);
    for (int k = 0; k < N; k++) for (int n = 0; n < N; n++)
        C[(size_t)k * N + n] = (n == 0) ? 0.5 : std::cos(PI / N * (k + 0.5) * n);
    return C;
}
static void matvec(const double* basis, const double* x, double* out, int N) {
    for (int k = 0; k < N; k++) {
        const double* b = &basis[(size_t)k * N]; double s = 0;
        for (int n = 0; n < N; n++) s += b[n] * x[n];
        out[k] = s;
    }
}
static void transpose(const std::vector<double>& A, std::vector<double>& T, int N) {
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++)
        T[(size_t)j * N + i] = A[(size_t)i * N + j];
}
static void dct_rows(const std::vector<double>& A, std::vector<double>& out, const std::vector<double>& C, int N) {
    for (int r = 0; r < N; r++) matvec(C.data(), &A[(size_t)r * N], &out[(size_t)r * N], N);
}
static void idxst_rows(const std::vector<double>& A, std::vector<double>& out, const std::vector<double>& Cidct, int N) {
    std::vector<double> t(N);
    for (int r = 0; r < N; r++) {
        const double* x = &A[(size_t)r * N];
        t[0] = x[0];
        for (int n = 1; n < N; n++) t[n] = x[N - n];
        double* o = &out[(size_t)r * N];
        matvec(Cidct.data(), t.data(), o, N);
        for (int k = 1; k < N; k += 2) o[k] = -o[k];
    }
}
// rho (NxN row-major double) -> Ex, Ey via the golden electrostatic pipeline.
static void golden_field(const std::vector<double>& rho, std::vector<double>& Ex,
                         std::vector<double>& Ey, int N) {
    const std::vector<double> Cdct = make_Cdct(N), Cidct = make_Cidct(N);
    std::vector<double> A((size_t)N*N), B((size_t)N*N), a_uv((size_t)N*N), Ex_hat((size_t)N*N), Ey_hat((size_t)N*N);
    // forward 2D DCT: a_uv = transpose(DCT_rows(transpose(DCT_rows(rho))))
    dct_rows(rho, B, Cdct, N); transpose(B, A, N);
    dct_rows(A, B, Cdct, N);   transpose(B, a_uv, N);
    // spectral: Ex_hat = a_uv*w_u/denom, Ey_hat = a_uv*w_v/denom, [0][0]=0
    const double TWO_PI = 2 * PI;
    for (int u = 0; u < N; u++) {
        const double w_u = TWO_PI * u / N;
        for (int v = 0; v < N; v++) {
            const double w_v = TWO_PI * v / N; const size_t i = (size_t)u * N + v;
            if (u == 0 && v == 0) { Ex_hat[i] = 0; Ey_hat[i] = 0; continue; }
            const double denom = w_u*w_u + w_v*w_v;
            Ex_hat[i] = a_uv[i] * w_u / denom;  Ey_hat[i] = a_uv[i] * w_v / denom;
        }
    }
    Ex.assign((size_t)N*N, 0); Ey.assign((size_t)N*N, 0);
    // inverse: Ex = transpose(IDXST_rows(transpose(IDCT_rows(Ex_hat)))); reuse dct_rows w/ Cidct for IDCT
    dct_rows(Ex_hat, A, Cidct, N); transpose(A, B, N); idxst_rows(B, A, Cidct, N); transpose(A, Ex, N);
    idxst_rows(Ey_hat, A, Cidct, N); transpose(A, B, N); dct_rows(B, A, Cidct, N); transpose(A, Ey, N);
}

// Golden rho: scatter each node's overlap area into bins (mirrors bin_scatter; all movable,
// no fixed/clamp), rho = overlap / bin_area. NxN row-major, x-major idx = col*N + row.
static void scatter_ref(const std::vector<NodeBox>& node_box, float bin_w, float bin_h,
                        std::vector<double>& rho, int N) {
    rho.assign((size_t)N * N, 0.0);
    const double bin_area = (double)bin_w * bin_h;
    for (const NodeBox& nd : node_box) {
        const double xl = nd.x, yl = nd.y, xh = nd.x + nd.w, yh = nd.y + nd.h;
        int col_lo = (int)(xl / bin_w); if (col_lo < 0) col_lo = 0;
        int col_hi = (int)(xh / bin_w); if (col_hi > N - 1) col_hi = N - 1;
        int row_lo = (int)(yl / bin_h); if (row_lo < 0) row_lo = 0;
        int row_hi = (int)(yh / bin_h); if (row_hi > N - 1) row_hi = N - 1;
        if (col_lo == col_hi && row_lo == row_hi) {
            rho[(size_t)col_lo * N + row_lo] += (double)nd.w * nd.h;
            continue;
        }
        for (int col = col_lo; col <= col_hi; col++) {
            const double lx = col * (double)bin_w, rx = lx + bin_w;
            const double ox = (xh < rx ? xh : rx) - (xl > lx ? xl : lx);
            if (ox <= 0) continue;
            for (int row = row_lo; row <= row_hi; row++) {
                const double ly = row * (double)bin_h, ry = ly + bin_h;
                const double oy = (yh < ry ? yh : ry) - (yl > ly ? yl : ly);
                if (oy <= 0) continue;
                rho[(size_t)col * N + row] += ox * oy;
            }
        }
    }
    for (auto& r : rho) r /= bin_area;
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

// Stage 5b: end-to-end density gradient (density_bin -> 2D DCT -> spectral -> inverse ->
// force_gather) vs an independent golden: scatter the SAME nodes -> rho -> naive field ->
// gather. Validates the composition (data crossing/orientation/scale) on top of the
// individually-verified stages. Synthetic all-movable nodes (no fixed/clamp path here;
// that is covered by the Stage 1 density verify).
int runDensityGradientVerify(const char* xclbin_path) {
    const int   N     = DENSITY_GRID;   // 1024
    const int   M     = 2000;           // movable nodes (all movable)
    const float bin_w = 1.0f, bin_h = 1.0f;
    const float target_density = 0.9f;  // no fixed nodes -> clamp inactive

    std::mt19937 rng(53);
    std::uniform_real_distribution<float> pos(0.0f, (float)(N - 16));
    std::uniform_real_distribution<float> siz(0.3f, 12.0f);
    std::vector<NodeBox> node_box(M);
    for (auto& nd : node_box) { nd.x = pos(rng); nd.y = pos(rng); nd.w = siz(rng); nd.h = siz(rng); }

    printf("[dgrad] verify: G=%d nodes=%d (density_bin -> 2D DCT -> spectral -> inverse -> force_gather)\n",
           N, M);

    std::vector<coord_t> dev(M);
    runDensityGradient(node_box.data(), M, M, bin_w, bin_h, target_density, dev.data(), xclbin_path);

    // ---- golden: scatter -> naive field -> gather (all host) ----
    std::vector<double> rho, ExD, EyD;
    scatter_ref(node_box, bin_w, bin_h, rho, N);
    golden_field(rho, ExD, EyD, N);
    std::vector<float> efx((size_t)N*N), efy((size_t)N*N);   // reuse the float gather helper
    for (size_t i = 0; i < (size_t)N*N; i++) { efx[i] = (float)ExD[i]; efy[i] = (float)EyD[i]; }

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
    printf("[dgrad] full density gradient vs scatter->field->gather golden: max_abs=%.3e  "
           "rel_rms=%.3e  -> %s\n", max_abs, rr, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace plalgo
