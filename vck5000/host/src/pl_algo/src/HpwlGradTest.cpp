// HpwlGradTest.cpp -- see HpwlGradTest.hpp. Throwaway Milestone B fixture.

#include "HpwlGradTest.hpp"
#include "Driver.hpp"
#include "host_interface.hpp"

#include <vector>
#include <utility>
#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>

namespace plalgo {

// 1-D weighted-average wirelength partials with EXACT exp at AIE_INV_GAMMA.
// Mirrors markv1 computeHpwlPartials_CPU (per axis). Returns partial[i] for c[i].
static std::vector<float> wa_partials_1d(const std::vector<float>& c) {
    const float ig = AIE_INV_GAMMA;
    const int n = (int)c.size();
    float mx = c[0], mn = c[0];
    for (float v : c) { mx = std::max(mx, v); mn = std::min(mn, v); }
    std::vector<float> ap(n), am(n);
    float Bp = 0, Bm = 0, Cp = 0, Cm = 0;
    for (int i = 0; i < n; i++) {
        ap[i] = std::exp((c[i] - mx) * ig);
        am[i] = std::exp((mn - c[i]) * ig);
        Bp += ap[i]; Bm += am[i];
        Cp += ap[i] * c[i]; Cm += am[i] * c[i];
    }
    std::vector<float> part(n);
    for (int i = 0; i < n; i++) {
        part[i] = ((1 + c[i] * ig) * Bp - Cp * ig) * (ap[i] / (Bp * Bp))
                - ((1 - c[i] * ig) * Bm + Cm * ig) * (am[i] / (Bm * Bm));
    }
    return part;
}

int runHpwlGradTest(const char* xclbin_path) {
    const int D = 5;               // net degree (within [AIE_NET_MIN, AIE_NET_MAX])
    const int G = NETS_PER_GROUP;  // 4 nets share the 8 SIMD lanes (x,y interleaved)
    const int groups = 1;

    // Synthetic nets: G nets of degree D, coords spread in [1,16] so the
    // exponents stay well-conditioned for fast_exp (gamma = 1/AIE_INV_GAMMA = 4).
    std::vector<std::vector<std::pair<float, float>>> nets(
        G, std::vector<std::pair<float, float>>(D));
    for (int k = 0; k < G; k++)
        for (int t = 0; t < D; t++)
            nets[k][t] = { 1.0f + (float)((k * 3 + t * 2) % 15),
                           2.0f + (float)((k * 2 + t * 5) % 13) };

    const int in_floats  = AIE_VEC * (1 + groups * D); // ctrl beat + term beats
    const int out_floats = AIE_VEC * (groups * D);     // no ctrl beat on output
    std::vector<float> in(in_floats, 0.0f), out(out_floats, 0.0f);
    std::vector<float> expect(out_floats, 0.0f);

    in[0] = (float)D;       // control beat
    in[1] = (float)groups;

    const int base = AIE_VEC; // term beats start after the control beat
    for (int k = 0; k < G; k++) {
        std::vector<float> xs(D), ys(D);
        for (int t = 0; t < D; t++) { xs[t] = nets[k][t].first; ys[t] = nets[k][t].second; }
        // Sort contract: max coord first, min second; x and y sorted independently.
        std::sort(xs.begin(), xs.end(), std::greater<float>());
        std::sort(ys.begin(), ys.end(), std::greater<float>());

        std::vector<float> px = wa_partials_1d(xs);
        std::vector<float> py = wa_partials_1d(ys);

        for (int t = 0; t < D; t++) {
            in[base + t * AIE_VEC + 2 * k    ] = xs[t]; // lane 2k   = net k x
            in[base + t * AIE_VEC + 2 * k + 1] = ys[t]; // lane 2k+1 = net k y
            expect[t * AIE_VEC + 2 * k    ] = px[t];    // dW/dx
            expect[t * AIE_VEC + 2 * k + 1] = py[t];    // dW/dy
        }
    }

    printf("[hpwl_grad] fixture: D=%d nets=%d in_floats=%d out_floats=%d\n",
           D, G, in_floats, out_floats);

    runHpwlGradPacket(in.data(), in_floats, out.data(), out_floats, xclbin_path);

    // Tolerance compare -- fast_exp is a 16-square approximation, not exact exp.
    double max_abs = 0, max_rel = 0, sse = 0;
    for (int i = 0; i < out_floats; i++) {
        double e = expect[i], a = out[i], err = std::fabs(a - e);
        max_abs = std::max(max_abs, err);
        sse += err * err;
        if (std::fabs(e) > 1e-4) max_rel = std::max(max_rel, err / std::fabs(e));
    }
    const double rms = std::sqrt(sse / out_floats);
    const double TOL = 2e-2; // 2%: accommodates the fast_exp approximation
    const bool pass = (max_rel < TOL);
    printf("[hpwl_grad] max_abs=%.3e  max_rel=%.3e  rms=%.3e  -> %s\n",
           max_abs, max_rel, rms, pass ? "PASS" : "FAIL");
    for (int i = 0; i < std::min(out_floats, 8); i++)
        printf("    [%d] device=%.6f  golden=%.6f\n", i, out[i], expect[i]);

    return pass ? 0 : 1;
}

} // namespace plalgo
