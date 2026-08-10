#ifndef PL_ALGO_PLACEMENT_HPP
#define PL_ALGO_PLACEMENT_HPP

// Placement.hpp -- Stage 5c.5 host orchestration policy + the runPlacement boundary.
//
// The ePlace host policy (λ init/update, γ schedule, preconditioner, Barzilai-Borwein step,
// Nesterov momentum coefficient) lives here as pure POD/vector math -- no XRT, no Limbo, no
// std::string -- so it is ABI-neutral and can be included by BOTH the old-ABI setup side
// (main.cpp, which derives the statics from the DataBase) and the new-ABI driver
// (Driver.cpp runPlacement, which owns the single device/graph session). Every formula
// mirrors sw_only (AIEplace.cpp), cross-checked against Xplace/DREAMPlace in the 5c audit.
//
// Single-session constraint: sw_emu cannot reopen the device/AIE-sim within one process, so
// the whole iteration loop runs inside one runPlacement call (device+graph opened once); these
// helpers are invoked between the device passes.

#include "host_interface.hpp"
#include <cmath>
#include <cstdint>

namespace plalgo {

// STEP_NORM for the exp LUT (matches HpwlGradVerify: lut[i]=exp(-i*STEP_NORM), so
// inv_lut_step = 1/(STEP_NORM*gamma) makes the lookup ~ exp(-d/gamma)).
static constexpr float PLACE_STEP_NORM = 0.05f;

struct PlacementConfig {
    int   max_iters;
    float die_x, die_y;          // die extents (origin 0,0); clamp upper bound is die - size
    float bin_w, bin_h;          // die/GRID
    float target_density;        // overflow capacity fraction (e.g. 0.9)
    float base_gamma;            // HPWL sharpness base (gamma = base, or 10*base if scheduled)
    int   gamma_schedule;        // 0/1
    float init_step_seed;        // seed for the iteration-1 BB step estimate, in SITE WIDTHS
                                 // (XPlace args.lr's unit -- it prescales coords by site width)
    float site_width;            // DBU per placement site; converts init_step_seed to DBU (TODO #23)
    float density_weight_init_multiplier;
    int   enable_momentum;       // 0/1
    // --- density-weight schedule + convergence (ported from sw_only) ---
    float density_weight_min_step;   // mu lower clamp (0.95)
    float density_weight_max_step;   // mu growth base (1.05)
    float overflow_threshold;        // stop overflow (0.07, = XPlace --stop_overflow)
    int   min_iters;                 // convergence_min_iterations (50)
    int   conv_iters;                // extra iters after overflow first drops below stop (30)
    // Preconditioner (sw_only updatePrecondWeights). Tri-state: -1 = auto (ON iff the design has
    // movable macros, matching sw_only's smart default), 0 = force OFF, 1 = force ON.
    int   enable_preconditioning = -1;
};

// ---- sw_only initializeDensityWeight: λ = (Σ|g_wl| / Σ|g_density|) * init_multiplier ----
static inline float initDensityWeight(const coord_t* g_wl, const coord_t* g_density,
                                      int M, float init_multiplier) {
    double wl_L1 = 0.0, den_L1 = 0.0;
    for (int n = 0; n < M; n++) {
        wl_L1  += std::fabs(g_wl[n].x)      + std::fabs(g_wl[n].y);
        den_L1 += std::fabs(g_density[n].x) + std::fabs(g_density[n].y);
    }
    return (float)(wl_L1 / (den_L1 + 1e-8)) * init_multiplier;
}

// ---- sw_only computeLipshitzEstimate: α = ||Δv|| / ||Δg_total|| ----
// No magnitude clamp -- mirrors XPlace (nesterov_optimizer.py), which bounds the step with its
// backtracking line search alone. The old fixed [1e-4, 4000] clamp was removed in sw_only
// (commit ba9cf41); the estimate self-scales with preconditioning, so a clamp only starves the
// step when precond is on. Ported to match the golden exactly.
static inline float bbStepLength(const coord_t* v, const coord_t* v_prev,
                                 const coord_t* g, const coord_t* g_prev, int M) {
    double pos2 = 0.0, grad2 = 0.0;
    for (int n = 0; n < M; n++) {
        const double dx = (double)v[n].x - v_prev[n].x, dy = (double)v[n].y - v_prev[n].y;
        const double gx = (double)g[n].x - g_prev[n].x, gy = (double)g[n].y - g_prev[n].y;
        pos2 += dx*dx + dy*dy; grad2 += gx*gx + gy*gy;
    }
    return (float)(std::sqrt(pos2) / std::sqrt(grad2 + 1e-8));
}

// ---- sw_only updateGamma: γ = 10^((overflow-0.1)*20/9 - 1) * base_gamma ----
static inline float updateGammaValue(float overflow, float base_gamma) {
    return std::pow(10.0f, (overflow - 0.1f) * (20.0f / 9.0f) - 1.0f) * base_gamma;
}

// ---- sw_only density_force_fraction (force-magnitude ratio) ----
// dff = ||λ·g_density||_1 / (||g_wl||_1 + ||λ·g_density||_1) in [0,1]: how balanced the
// wirelength and density forces are. Field-norm/scale invariant. Gates the shared skip_update.
static inline float densityForceFraction(const coord_t* g_wl, const coord_t* g_density,
                                         int M, float lambda) {
    double wl = 0.0, den = 0.0;
    for (int n = 0; n < M; n++) {
        wl  += std::fabs(g_wl[n].x)      + std::fabs(g_wl[n].y);
        den += std::fabs(g_density[n].x) + std::fabs(g_density[n].y);
    }
    den *= lambda;
    return (float)(den / (wl + den + 1e-8));
}

// ---- sw_only updateDensityWeight: scale-invariant multiplicative λ trend ----
// mu grows λ near max_step while HPWL improves; while it worsens, damp with the RELATIVE form
// (dHPWL/prev_hpwl, dimensionless). sw_only reverted the fixed-K form (ad6d52a) as mis-scaled for
// std-cell HPWL. The caller applies the shared skip_update gate (freeze λ and γ together).
static inline float updateDensityWeight(float lambda, float hpwl, float prev_hpwl, int iteration,
                                        float min_step, float max_step) {
    const float dHPWL = hpwl - prev_hpwl;
    float mu;
    if (dHPWL < 0.0f) {
        const float decay = std::pow(0.9999f, (float)iteration);
        mu = max_step * (decay > 0.98f ? decay : 0.98f);
    } else {
        const float rel = dHPWL / (prev_hpwl + 1e-8f);
        float g = std::pow(max_step, -rel * 100.0f);
        if (g < min_step) g = min_step;
        if (g > max_step) g = max_step;
        mu = max_step * g;
    }
    return lambda * mu;
}

// ---- shared skip_update gate (sw_only performIteration / XPlace step()): on 2 of every 3 iters,
// while early (k<50) or the WL/density forces are mid-balance (dff in (0.5,0.95)), FREEZE λ+γ. ----
static inline bool scheduleSkipUpdate(int iteration, float dff) {
    return ((iteration < 50) || (dff > 0.5f && dff < 0.95f)) && (iteration % 3 != 0);
}

// ---- sw_only performNextStep momentum: a_{k+1}=(1+sqrt(4a^2+1))/2; coeff=(a_k-1)/a_{k+1} ----
static inline float momentumCoeff(float& nesterov_ak, int enable_momentum) {
    const float a_next = (1.0f + std::sqrt(4.0f * nesterov_ak * nesterov_ak + 1.0f)) / 2.0f;
    const float coeff  = enable_momentum ? (nesterov_ak - 1.0f) / a_next : 0.0f;
    nesterov_ak = a_next;
    return coeff;
}

// ---- sw_only updatePrecondWeights: w = max(1, num_pins + precond_coef*λ*area) ----
// RAW node area, matching XPlace alpha_2 = pcoef*λ*mov_node_area: both run in the same raw-DBU
// frame, so the area term needs no normalization (sw_only Schedule.cpp).
static inline void updatePrecondWeights(float* precond, const int32_t* degree, const float* area,
                                        int M, float precond_coef, float lambda) {
    const float lambda_area_coef = precond_coef * lambda;
    for (int n = 0; n < M; n++) {
        float w = (float)degree[n] + lambda_area_coef * area[n];
        precond[n] = w > 1.0f ? w : 1.0f;
    }
}

// IGNORE_NET_DEGREE (XPlace net_mask) lives in host_interface.hpp (shared host<->PL contract).

// ---- host HPWL (mirrors metrics.hpp / DataBase::computeTotalWirelength): sum_nets bbox
// half-perimeter, over nets with 2 <= degree <= IGNORE_NET_DEGREE ----
static inline double hostHPWL(const coord_t* node_pos, const int32_t* net_ptr,
                              const NodePin* pins, int num_nets) {
    double total = 0.0;
    for (int net = 0; net < num_nets; net++) {
        const int beg = net_ptr[net], end = net_ptr[net + 1];
        const int deg = end - beg;
        if (deg <= 1 || deg > IGNORE_NET_DEGREE) continue;   // XPlace net_mask
        float maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
        for (int p = beg; p < end; p++) {
            const NodePin& r = pins[p];
            const float x = node_pos[r.node_idx].x + r.off_x;
            const float y = node_pos[r.node_idx].y + r.off_y;
            if (x > maxx) maxx = x; if (x < minx) minx = x;
            if (y > maxy) maxy = y; if (y < miny) miny = y;
        }
        total += (double)((maxx - minx) + (maxy - miny));
    }
    return total;
}

// ---- host overflow (mirrors sw_only Grid::computeTotalOverflow) ----
// rho is x-major [GRID*GRID]; overflow = bin_area * sum max(0, rho-target) / movable_area.
static inline double hostOverflow(const float* rho, int nbins, float target,
                                  float bin_area, float total_movable_area) {
    double s = 0.0;
    for (int b = 0; b < nbins; b++) { const double e = (double)rho[b] - target; if (e > 0.0) s += e; }
    return s * bin_area / (total_movable_area + 1e-8);
}

} // namespace plalgo

#endif // PL_ALGO_PLACEMENT_HPP
