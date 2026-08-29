// bb_reduce_test.cpp -- verify bb_reduce.hpp against a double-precision reference of the
// Barzilai-Borwein reduction. bb_reduce is the on-device replacement for the host BB step
// (sw_only computeLipschitzEstimate + combineGradients), and it is what removes the last
// per-iteration host round-trip in the control path -- i.e. the module that makes the device-
// resident loop possible (DATAFLOW.md; TODO #20 step 3). Over the movable nodes it computes:
//
//   pos_norm_sq   = sum ||v_cur - v_prev||^2
//   g_total[n]    = g_hpwl[n] - lambda * g_density[n]                    (the combined gradient)
//   grad_norm_sq  = sum || (1/precond[n]) * (g_total[n] - g_total_prev[n]) ||^2
//
// The golden is that same math in double, computed inline here -- NOT sw_only's function, which is
// a Placer method wired into Node/db and would drag the host object graph into a pure-g++ harness
// (the same reason hpwl_grad_test transcribes its golden). The preconditioned delta is the
// raw-vs-preconditioned consistency fix from the golden (Step.cpp computeLipschitzEstimate divides
// the gradient DIFFERENCE by precond, matching the map Node::step actually moves).
//
// THREE ASSERTIONS (a test asserts -- it computes the verdict and exits 0/non-zero, and every
// number it prints as evidence is also compared in code):
//   [1] pos_norm_sq  -- float sequential sum vs double reference.
//   [2] grad_norm_sq -- exercises the preconditioned delta; precond mixes 1.0 (raw) and >1.
//   [3] g_total_out  -- computed per element in float exactly as the module does -> BIT-EXACT.
//
// Tolerances: [1]/[2] are float reductions over M~4000 terms against a double reference; observed
// rel ~1e-7, so the bound is 1e-5 -- >100x margin, which passes legitimate float rounding while a
// restructuring bug (wrong sign, missing 1/precond, dropped term) lands at 1e-1..1e0. [3] is
// bit-exact: the golden uses the identical float op, so any nonzero difference is a real defect.
//
// Build: g++ -std=c++17 -O2 -I../pl/src/pl_algo/src bb_reduce_test.cpp -o bb_reduce_test

#include "modules/bb_reduce.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
#include <random>

using namespace plalgo;

int main() {
    const int M = 4000;
    const float lambda = 1.4f;                        // a realistic mid-run density weight

    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> pos(0.0f, 10000.0f);
    std::uniform_real_distribution<float> grd(-50.0f, 50.0f);
    std::uniform_real_distribution<float> pcd(1.0f, 4.0f);
    std::bernoulli_distribution precond_on(0.5);      // half the nodes preconditioned, half raw

    std::vector<coord_t> v_cur(M), v_prev(M), g_hpwl(M), g_density(M), g_total_prev(M), g_total_out(M);
    std::vector<float>   precond(M);
    for (int n = 0; n < M; n++) {
        v_cur[n]        = {pos(rng), pos(rng)};
        v_prev[n]       = {pos(rng), pos(rng)};
        g_hpwl[n]       = {grd(rng), grd(rng)};
        g_density[n]    = {grd(rng), grd(rng)};
        g_total_prev[n] = {grd(rng), grd(rng)};
        precond[n]      = precond_on(rng) ? pcd(rng) : 1.0f;   // exercise inv_p == 1 and inv_p < 1
    }

    float pos_ps = 0.0f, grad_ps = 0.0f;
    bb_reduce(v_cur.data(), v_prev.data(), g_hpwl.data(), g_density.data(), g_total_prev.data(),
              precond.data(), lambda, M, g_total_out.data(), &pos_ps, &grad_ps);

    // Double-precision reference for the two norms; float reference (identical op) for g_total.
    double pos_gold = 0.0, grad_gold = 0.0;
    int    gtot_mismatches = 0;
    for (int n = 0; n < M; n++) {
        double dvx = (double)v_cur[n].x - v_prev[n].x, dvy = (double)v_cur[n].y - v_prev[n].y;
        pos_gold += dvx*dvx + dvy*dvy;

        double gx = (double)g_hpwl[n].x - (double)lambda * g_density[n].x;
        double gy = (double)g_hpwl[n].y - (double)lambda * g_density[n].y;
        double inv_p = 1.0 / (double)precond[n];
        double dgx = inv_p * (gx - g_total_prev[n].x), dgy = inv_p * (gy - g_total_prev[n].y);
        grad_gold += dgx*dgx + dgy*dgy;

        // g_total: the module does gx = g_hpwl - lambda*g_density in float; replicate exactly.
        float gx_f = g_hpwl[n].x - lambda * g_density[n].x;
        float gy_f = g_hpwl[n].y - lambda * g_density[n].y;
        if (g_total_out[n].x != gx_f || g_total_out[n].y != gy_f) gtot_mismatches++;
    }

    auto rel = [](double got, double ref) {
        double s = std::fabs(ref);
        return s > 1e-30 ? std::fabs(got - ref) / s : std::fabs(got - ref);
    };
    double e_pos = rel(pos_ps, pos_gold), e_grad = rel(grad_ps, grad_gold);

    const double NORM_TOL = 1e-5;
    bool ok1 = e_pos  < NORM_TOL;
    bool ok2 = e_grad < NORM_TOL;
    bool ok3 = (gtot_mismatches == 0);

    printf("[info] design: %d movable nodes, lambda=%.3f, ~50%% preconditioned\n", M, lambda);
    printf("[1] pos_norm_sq   module=%.9e golden=%.9e  rel=%.3e (tol %.0e)  %s\n",
           pos_ps, pos_gold, e_pos, NORM_TOL, ok1 ? "ok" : "FAIL");
    printf("[2] grad_norm_sq  module=%.9e golden=%.9e  rel=%.3e (tol %.0e)  %s\n",
           grad_ps, grad_gold, e_grad, NORM_TOL, ok2 ? "ok" : "FAIL");
    printf("[3] g_total       %d / %d elements differ from the float reference (bit-exact) %s\n",
           gtot_mismatches, M, ok3 ? "ok" : "FAIL");

    bool ok = ok1 && ok2 && ok3;
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
