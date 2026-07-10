// IterVerify.cpp -- see IterVerify.hpp.
//
// Old-ABI TU (like ForceVerify): the golden Nesterov step is computed here in double; the
// new-ABI XRT Driver is reached only through runIterUpdate's POD + const char* boundary.
// The golden replicates, in order, sw_only's per-node update:
//   combineGradients : g_total = g_wl - lambda*g_density
//   Node::step       : pg = g_total/precond; u = v_k - alpha*pg; v' = u + coeff*(u - u_k)
//   enforceDieBounds : clamp u and v' independently into [0, die - size]
// A wrong combine sign, a missing preconditioner, or the wrong clamp order would each shift
// the stepped positions, so a PASS validates all three (5c.1 combine + 5c.2 step).

#include "IterVerify.hpp"
#include "Driver.hpp"
#include "host_interface.hpp"

#include <vector>
#include <cmath>
#include <cstdio>
#include <random>

namespace plalgo {

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int runIterUpdateVerify(const char* xclbin_path) {
    const int   M        = 4000;
    const float die_x    = 1024.0f, die_y = 1024.0f;
    const float lambda   = 0.3f;    // density weight
    const float alpha    = 0.7f;    // BB step
    const float coeff    = 0.42f;   // Nesterov momentum coeff

    std::mt19937 rng(97);
    std::uniform_real_distribution<float> pos(0.0f, 1000.0f);   // v_k anchor (leaves room for size)
    std::uniform_real_distribution<float> siz(1.0f, 20.0f);
    std::uniform_real_distribution<float> grd(-2.0f, 2.0f);
    std::uniform_real_distribution<float> prc(1.0f, 6.0f);
    std::uniform_real_distribution<float> jit(-8.0f, 8.0f);     // u_k jitter around v_k

    std::vector<NodeBox> node_box(M);
    std::vector<coord_t> u_k(M), g_hpwl(M), g_density(M);
    std::vector<float>   precond(M);
    for (int n = 0; n < M; n++) {
        node_box[n].x = pos(rng); node_box[n].y = pos(rng);       // v_k
        node_box[n].w = siz(rng); node_box[n].h = siz(rng);       // size
        u_k[n].x = node_box[n].x + jit(rng);
        u_k[n].y = node_box[n].y + jit(rng);
        g_hpwl[n].x = grd(rng); g_hpwl[n].y = grd(rng);
        g_density[n].x = grd(rng); g_density[n].y = grd(rng);
        precond[n] = prc(rng);
    }

    printf("[iter] verify: M=%d die=%.0fx%.0f lambda=%.3f alpha=%.3f coeff=%.3f "
           "(combine + precond + BB step + momentum + clamp)\n",
           M, die_x, die_y, lambda, alpha, coeff);

    std::vector<coord_t> u_out(M), v_out(M);
    runIterUpdate(M, g_hpwl.data(), g_density.data(), node_box.data(), u_k.data(),
                  precond.data(), lambda, alpha, coeff, die_x, die_y,
                  u_out.data(), v_out.data(), xclbin_path);

    double sse = 0, ref = 0, max_abs = 0;
    int    clamped = 0;
    for (int n = 0; n < M; n++) {
        const double gx = (double)g_hpwl[n].x - (double)lambda * g_density[n].x;
        const double gy = (double)g_hpwl[n].y - (double)lambda * g_density[n].y;
        const double pgx = gx / precond[n], pgy = gy / precond[n];
        const double uxr = (double)node_box[n].x - (double)alpha * pgx;
        const double uyr = (double)node_box[n].y - (double)alpha * pgy;
        const double vxr = uxr + (double)coeff * (uxr - u_k[n].x);
        const double vyr = uyr + (double)coeff * (uyr - u_k[n].y);
        const double mx = (double)die_x - node_box[n].w;
        const double my = (double)die_y - node_box[n].h;
        const double gux = clampd(uxr, 0.0, mx), guy = clampd(uyr, 0.0, my);
        const double gvx = clampd(vxr, 0.0, mx), gvy = clampd(vyr, 0.0, my);
        if (gux != uxr || guy != uyr || gvx != vxr || gvy != vyr) clamped++;

        const double du0 = (double)u_out[n].x - gux, du1 = (double)u_out[n].y - guy;
        const double dv0 = (double)v_out[n].x - gvx, dv1 = (double)v_out[n].y - gvy;
        sse += du0*du0 + du1*du1 + dv0*dv0 + dv1*dv1;
        ref += gux*gux + guy*guy + gvx*gvx + gvy*gvy;
        max_abs = std::max(max_abs, std::max(std::max(std::fabs(du0), std::fabs(du1)),
                                             std::max(std::fabs(dv0), std::fabs(dv1))));
    }
    const double rr = std::sqrt(sse / (ref + 1e-30));
    const bool   ok = rr < 1e-5;
    printf("[iter] Nesterov step vs sw_only golden: %d/%d clamped  max_abs=%.3e  rel_rms=%.3e  -> %s\n",
           clamped, M, max_abs, rr, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace plalgo
