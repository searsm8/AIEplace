// sched_verify.cpp -- offline verification of the PL param_scheduler against the sw_only golden.
//
// Replays a recorded schedule_trace.csv (sw_only with "dump_schedule_trace": true) through
// modules/param_scheduler.hpp and asserts the four produced scalars (inv_gamma, alpha, coeff,
// lambda) match the golden row-for-row. Pure g++/offline -- no device, no XRT -- so the tuned
// scalar schedule is nailed before any sw_emu integration (PL_PORT_PLAN.md stages S3-S4).
//
// Build: g++ -std=c++17 -O2 -I../src sched_verify.cpp -o sched_verify
// Run:   ./sched_verify <schedule_trace.csv>
//
// dff_coef (the density_force_fraction closed-form constant, precond OFF) is DERIVED from the
// trace itself -- dff/(1-dff) = dff_coef * lambda_prev -- and its constancy across the run is
// itself a check on the closed form. lambda on iteration 1 is the golden init value (the L1 norms
// that produce it are not in the trace), so it is seeded, not recomputed; the trend is verified
// from iteration 2 on.

#include "modules/param_scheduler.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace plalgo;

struct Row {
    int iter; float hpwl, overflow, pos2, grad2, dff, base_gamma, gamma, inv_gamma,
        step_length, nesterov_ak, coeff, density_weight;
};

static std::vector<Row> load(const char* path) {
    std::vector<Row> rows;
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    std::string line; std::getline(f, line); // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line); std::string c; Row r; float v[13]; int i = 0;
        while (std::getline(ss, c, ',') && i < 13) v[i++] = std::stof(c);
        r.iter=(int)v[0]; r.hpwl=v[1]; r.overflow=v[2]; r.pos2=v[3]; r.grad2=v[4]; r.dff=v[5];
        r.base_gamma=v[6]; r.gamma=v[7]; r.inv_gamma=v[8]; r.step_length=v[9];
        r.nesterov_ak=v[10]; r.coeff=v[11]; r.density_weight=v[12];
        rows.push_back(r);
    }
    return rows;
}

static double relerr(double got, double ref) {
    double d = std::fabs(got - ref), s = std::fabs(ref);
    return s > 1e-30 ? d / s : d;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <schedule_trace.csv>\n", argv[0]); return 1; }
    auto rows = load(argv[1]);
    if (rows.size() < 2) { fprintf(stderr, "trace too short\n"); return 1; }

    // Derive dff_coef from the trace: dff/(1-dff) = c*lambda_prev, lambda_prev = density_weight[k-1].
    // Constancy of c is a check on the closed form itself.
    std::vector<double> cs;
    for (size_t i = 1; i < rows.size(); i++) {
        double dff = rows[i].dff, lam_prev = rows[i-1].density_weight;
        if (dff > 0 && dff < 1 && lam_prev > 0) cs.push_back((dff / (1.0 - dff)) / lam_prev);
    }
    std::sort(cs.begin(), cs.end());
    double c_med = cs[cs.size()/2], c_min = cs.front(), c_max = cs.back();
    printf("dff_coef: median=%.6g  min=%.6g  max=%.6g  (spread %.2g%%)\n",
           c_med, c_min, c_max, 100.0*(c_max-c_min)/c_med);

    SchedParams p;
    p.base_gamma = rows[0].base_gamma;
    p.min_step = 0.95f; p.max_step = 1.05f; p.init_multiplier = 8e-5f;
    p.dff_coef = (float)c_med; p.enable_momentum = 1; p.gamma_schedule = 1;
    // convergence config: the trace was produced with stop 0.04; the rest are sw_only defaults.
    p.overflow_threshold = 0.04f; p.min_iters = 50; p.max_iters = 1200;
    p.conv_iters = 30; p.max_life = 30;

    SchedState st; sched_state_init(st, p);

    double e_ig=0, e_al=0, e_co=0, e_la=0; int worst_ig=0, worst_la=0;
    int first_stop = -1, premature_stop = -1; // first iter my scheduler asserts stop
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        float inv_gamma, alpha, coeff, lambda; int stop;
        float g_wl = 0, g_den = 0;
        // Feed the golden density_force_fraction to isolate the lambda-trend logic; the closed
        // form that produces dff on the PL (sched_dff) is validated separately below.
        param_scheduler(st, p, r.hpwl, r.overflow, r.pos2, r.grad2, r.dff, g_wl, g_den,
                        inv_gamma, alpha, coeff, lambda, stop);
        if (stop) {
            if (first_stop < 0) first_stop = r.iter;
            if ((size_t)(i + 1) < rows.size() && premature_stop < 0) premature_stop = r.iter;
        }
        // iteration 1: the lambda-init L1 norms aren't in the trace, so seed both the state and the
        // compared value from the golden init (the trend is what we verify, from iteration 2 on).
        if (r.iter == 1) { st.lambda = r.density_weight; lambda = r.density_weight; }

        double eig = relerr(inv_gamma, r.inv_gamma);
        double eal = relerr(alpha, r.step_length);
        double eco = std::fabs(coeff - r.coeff);          // coeff is O(1), use absolute
        double ela = relerr(lambda, r.density_weight);
        if (eig > e_ig) { e_ig = eig; worst_ig = r.iter; }
        if (eal > e_al) e_al = eal;
        if (eco > e_co) e_co = eco;
        if (ela > e_la) { e_la = ela; worst_la = r.iter; }
    }

    printf("max rel err  inv_gamma=%.3e (iter %d)  alpha=%.3e  lambda=%.3e (iter %d)\n",
           e_ig, worst_ig, e_al, e_la, worst_la);
    printf("max abs err  coeff=%.3e\n", e_co);

    // Convergence: the golden run STOPPED at the last recorded iteration, so the scheduler's stop
    // flag must first fire exactly there -- not earlier (premature stop), not never.
    const int golden_stop = rows.back().iter;
    printf("stop flag: golden stopped at iter %d, scheduler first stop at iter %d%s\n",
           golden_stop, first_stop,
           premature_stop >= 0 ? "  [PREMATURE]" : (first_stop < 0 ? "  [NEVER]" : ""));

    // Closed-form dff fidelity: sched_dff(lambda_prev, c_med) vs golden density_force_fraction.
    // (The real PL uses the exact c = precond_coef*K/total_pins; here c is fit from the trace.)
    double e_dff = 0;
    for (size_t i = 1; i < rows.size(); i++) {
        float got = sched_dff(rows[i-1].density_weight, (float)c_med);
        double e = relerr(got, rows[i].dff);
        if (e > e_dff) e_dff = e;
    }
    printf("closed-form dff max rel err vs golden: %.3e\n", e_dff);

    const double TOL = 1e-4;
    bool sched_ok = e_ig < TOL && e_al < TOL && e_la < TOL && e_co < TOL;
    bool conv_ok  = (first_stop == golden_stop) && (premature_stop < 0);
    bool ok = sched_ok && conv_ok;
    printf("%s  (%zu iterations, tol %.0e; schedule %s, convergence %s)\n",
           ok ? "PASS" : "FAIL", rows.size(), TOL,
           sched_ok ? "ok" : "FAIL", conv_ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
