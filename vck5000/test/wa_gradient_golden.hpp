#ifndef WA_GRADIENT_GOLDEN_HPP
#define WA_GRADIENT_GOLDEN_HPP

// wa_gradient_golden.hpp -- the WA-wirelength gradient golden shared by the pin-record gradient
// harnesses (#41): sw_only's WA partial (computeHpwlPartials_CPU, Partials.cpp; the same form as
// hpwl_dhar_test's golden) evaluated in double straight from the PARSED netlist -- per in-scope
// net, bbox-shifted exponents, B/C sums, eq. 4 per pin, summed per parsed node. A macro's pins sum
// into the macro; fixed pins contribute nothing. Independent of every encoding choice. Meow.

#include "record_design.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace golden {

// Production LUT geometry (host/src/pl_algo/src/main.cpp, Placement.hpp), as hpwl_dhar_test.
constexpr float PLACE_STEP_NORM = 0.05f;
constexpr int   GAMMA_MULT      = 12;
constexpr float GAMMA           = 120.0f;
constexpr float EXTENT          = 4000.0f;   // positions ~33 gamma across: exps span the LUT

struct Lut {
    std::vector<float> table;
    int   size;
    float inv_step;
};

inline Lut make_lut() {
    Lut lut;
    lut.size = (int)(GAMMA_MULT / PLACE_STEP_NORM) + 2;
    for (int i = 0; i < lut.size; i++) lut.table.push_back(std::exp(-(float)i * PLACE_STEP_NORM));
    lut.inv_step = 1.0f / (PLACE_STEP_NORM * GAMMA);
    return lut;
}

// Independent re-implementation of the LUT interpolation, in double (as hpwl_dhar_test). Meow.
inline double lut_exp_ref(const Lut& lut, float d) {
    const float idx_f = d * lut.inv_step;
    const int   idx   = (int)idx_f;
    if (idx >= lut.size - 1) return 0.0;
    const double frac = idx_f - idx;
    return (double)lut.table[idx] * (1.0 - frac) + (double)lut.table[idx + 1] * frac;
}

inline std::vector<double> wa_gradient(const packer::Netlist& nl, int axis, const std::vector<float>& node_pos,
                                       const Lut& lut, bool use_lut) {
    const double inv_gamma = 1.0 / GAMMA;
    auto expo = [&](float d) { return use_lut ? lut_exp_ref(lut, d) : std::exp(-(double)d * inv_gamma); };
    std::vector<double> grad(nl.movable.size(), 0.0);
    for (const auto& net : nl.nets) {
        if (!packer::in_scope(net)) continue;
        std::vector<float> p;
        for (const packer::Pin& pin : net) p.push_back(fixture::pin_position(nl, pin, axis, node_pos));
        const float hi = *std::max_element(p.begin(), p.end()), lo = *std::min_element(p.begin(), p.end());
        std::vector<double> ap(p.size()), am(p.size());
        double Bp = 0, Cp = 0, Bm = 0, Cm = 0;
        for (size_t k = 0; k < p.size(); k++) {
            ap[k] = expo(hi - p[k]); am[k] = expo(p[k] - lo);
            Bp += ap[k]; Cp += ap[k] * p[k]; Bm += am[k]; Cm += am[k] * p[k];
        }
        for (size_t k = 0; k < p.size(); k++) {
            if (!nl.movable[net[k].node]) continue;
            const double x = p[k];
            grad[net[k].node] += ((1.0 + x * inv_gamma) * Bp - Cp * inv_gamma) * (ap[k] / (Bp * Bp))
                               - ((1.0 - x * inv_gamma) * Bm + Cm * inv_gamma) * (am[k] / (Bm * Bm));
        }
    }
    return grad;
}

struct Err { double rel_rms, max_rel; };

// `got[node]` is the device gradient of parsed movable node `node` (NaN counts as infinitely wrong).
inline Err compare(const packer::Netlist& nl, const std::vector<float>& got, const std::vector<double>& want) {
    double se = 0, sr = 0, worst = 0; long count = 0;
    for (size_t node = 0; node < nl.movable.size(); node++) {
        if (!nl.movable[node]) continue;
        const double err = (double)got[node] - want[node];
        se += err * err; sr += want[node] * want[node]; count++;
        worst = std::max(worst, std::fabs(err));
        if (std::isnan(got[node])) worst = INFINITY;
    }
    const double rms_ref = std::sqrt(sr / count);
    return Err{std::sqrt(se / count) / rms_ref, worst / rms_ref};
}

// Tolerances, as hpwl_dhar_test [1] (module vs same-LUT double golden) and [2] (vs true exp).
constexpr double STRUCT_RMS_TOL = 1e-5, STRUCT_MAX_TOL = 1e-4;
constexpr double LUT_RMS_TOL    = 1e-4, LUT_MAX_TOL    = 1e-3;

inline float span(const packer::Netlist& nl, int net, int axis, const std::vector<float>& node_pos) {
    float hi = -INFINITY, lo = INFINITY;
    for (const packer::Pin& pin : nl.nets[net]) {
        const float p = fixture::pin_position(nl, pin, axis, node_pos);
        hi = p > hi ? p : hi; lo = p < lo ? p : lo;
    }
    return hi - lo;
}

} // namespace golden

#endif // WA_GRADIENT_GOLDEN_HPP
