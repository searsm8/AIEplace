/**
 * @file Partials.cpp
 * @brief Weighted-average HPWL gradient (the wirelength partials dW/dx, dW/dy per node).
 *        Split out of AIEplace.cpp; provides the cpu / simple backends.
 */

#include "AIEplace.h"
#include <cmath>
#include <cassert>
#include <chrono>

AIEPLACE_NAMESPACE_BEGIN

/// @brief Clear per-node wirelength gradients, then dispatch to the configured backend
///        (cpu / simple) to accumulate the WA-HPWL partials for this iteration.
void Placer::computeHpwlPartials()
{
    // Clear probe_grad before accumulation, otherwise gradients compound across iterations.
    for (const auto& item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->next.probe_grad.clear();
    }
    for (auto filler_p : db.getFillers()) {
        filler_p->next.probe_grad.clear();
    }
    for (const auto& item : db.getIOPads()) {
        item.second->next.probe_grad.clear();
    }

    if(partials_method == "cpu") {
        computeHpwlPartials_CPU();
    }
    else if(partials_method == "simple") {
        computeHpwlPartials_simple();
    }
    else {
        Logger::log_error("Invalid partials_compute_method specified in config file "
                          "(sw_only supports 'cpu'/'simple' only; AIE acceleration lives in pl_algo)");
        exit(1);
    }
}

/***************
 * CPU FUNCTIONS
 ****************/

/**
 * @brief Build the normalized exp(-x) lookup table for the 'simple' HPWL gradient.
 *        Stores exp(-i * LUT_STEP_NORM) for x = d/gamma. Built once; only inv_lut_step and
 *        hpwl_lut_range depend on gamma and are refreshed by updateGamma() per iteration.
 */
void Placer::initHpwlLut()
{
    hpwl_lut_size = int(LUT_GAMMA_MULTIPLIER / LUT_STEP_NORM) + 2; // fixed: 52
    hpwl_lut.resize(hpwl_lut_size);
    for (int i = 0; i < hpwl_lut_size; i++)
        hpwl_lut[i] = exp(-i * LUT_STEP_NORM);
    // Set gamma-dependent scalars for the initial gamma value
    hpwl_lut_range = LUT_GAMMA_MULTIPLIER * gamma;
    inv_lut_step   = 1.0f / (LUT_STEP_NORM * gamma);
    Logger::log_detail("HPWL LUT initialized: " + std::to_string(hpwl_lut_size)
        + " entries (normalized), init_gamma=" + std::to_string(gamma));
}

/**
 * @brief Update the WA smoothing length gamma on the XPlace overflow-driven schedule and
 *        refresh the LUT scalars. gamma = 10^((overflow - 0.1) * 20/9 - 1) * base_gamma
 *        (overflow 1.0 -> ~10x base; 0.55 -> 1x; 0.07 -> ~0.09x).
 */
void Placer::updateGamma(float overflow)
{
    if (!gamma_schedule) return;
    float coef = std::pow(10.0f, (overflow - 0.1f) * (20.0f / 9.0f) - 1.0f);
    gamma     = coef * base_gamma;
    inv_gamma = 1.0f / gamma;
    hpwl_lut_range = LUT_GAMMA_MULTIPLIER * gamma;
    inv_lut_step   = 1.0f / (LUT_STEP_NORM * gamma);
}

/// @brief Linearly interpolate into the precomputed exp(-d/gamma) LUT.
inline float Placer::lutLookup(float d) const
{
    float idx_f = d * inv_lut_step;
    int idx = int(idx_f);
    float frac = idx_f - idx;
    return hpwl_lut[idx] * (1.0f - frac) + hpwl_lut[idx + 1] * frac;
}

/**
 * @brief Fast LUT-based WA-HPWL gradient approximation (2-node softmax): for a node at
 *        distance d_max from the net's max edge and d_min from the min edge,
 *        grad ≈ [exp(-d_max/γ) - exp(-d_min/γ)] / [1 + exp(-span/γ)], exp() from the LUT.
 *        Nodes farther than 5γ from both edges contribute ≈0 and are skipped.
 */
void Placer::computeHpwlPartials_simple()
{
    TIME_FUNCTION();

    const float range = hpwl_lut_range;

    int ignore_net_degree = cfg["params"]["ignore_net_degree"].value_or(100); // XPlace net_mask
    for (Net* net_p : db.getNetsVector()) {
        const std::vector<NetPin>& pins = net_p->getPins();
        int net_size = net_p->getDegree();
        if (net_size <= 1 || net_size > ignore_net_degree) continue;

        // Find bounding box using pin positions (node + offset)
        float min_x = __FLT_MAX__, min_y = __FLT_MAX__;
        float max_x = -__FLT_MAX__, max_y = -__FLT_MAX__;
        for (const NetPin& pin : pins) {
            Position p = pin.getProbePos();
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
        }

        float span_x = max_x - min_x;
        float span_y = max_y - min_y;

        // Normalization: 1/(1+exp(-span/γ)) — corrects for small-span nets
        // where the gradient magnitude should be < 1.
        float norm_x = (span_x < range) ? 1.0f / (1.0f + lutLookup(span_x)) : 1.0f;
        float norm_y = (span_y < range) ? 1.0f / (1.0f + lutLookup(span_y)) : 1.0f;

        for (const NetPin& pin : pins) {
            Position p = pin.getProbePos();

            float d_max_x = max_x - p.x;
            float d_min_x = p.x - min_x;
            float d_max_y = max_y - p.y;
            float d_min_y = p.y - min_y;

            float plus_x  = (d_max_x < range) ? lutLookup(d_max_x) * norm_x : 0.0f;
            float minus_x = (d_min_x < range) ? lutLookup(d_min_x) * norm_x : 0.0f;

            float plus_y  = (d_max_y < range) ? lutLookup(d_max_y) * norm_y : 0.0f;
            float minus_y = (d_min_y < range) ? lutLookup(d_min_y) * norm_y : 0.0f;

            // Gradient accumulates onto the parent node
            pin.node_p->next.probe_grad.x += plus_x - minus_x;
            pin.node_p->next.probe_grad.y += plus_y - minus_y;
        }
    }
}

/*
 * Divergence diagnostics for computeHpwlPartials_CPU. Both dump the full local state of the
 * offending net so a failure is debuggable from the log alone; both are cold paths that end the
 * run, so they are kept out of the gradient loop to leave the math readable.
 * (Non-const refs because Term/Net to_string() and getName() are not const in this codebase.)
 */

/// @brief Log a zero B term: every A term of that net underflowed to 0, so the WA denominator
///        vanished. Usually means gamma has collapsed or pin coordinates have blown up.
static void logZeroBTermDiagnostic(Net* net_p, const std::vector<NetPin>& pins,
                                   std::vector<Term>& A, Term& B,
                                   float min_x, float max_x, float min_y, float max_y)
{
    int net_size = (int)pins.size();
    Logger::log_error("Zero value detected in B terms, cannot compute partials for net " + net_p->getName());
    Logger::log_error("B: " + B.to_string());
    Logger::log_error(net_p->to_string());
    for (int i = 0; i < net_size; i++)
        Logger::log_error("A[" + std::to_string(i) + "]: " + A[i].to_string() + " node: " + pins[i].node_p->getName());
    Logger::log_error("max_x: " + std::to_string(max_x) + " min_x: " + std::to_string(min_x) +
                      " max_y: " + std::to_string(max_y) + " min_y: " + std::to_string(min_y));
    Logger::log_error("Pin positions:");
    for (int i = 0; i < net_size; i++) {
        Position p = pins[i].getProbePos();
        Logger::log_error("Node " + pins[i].node_p->getName() +
                          " pin_x: " + std::to_string(p.x) + " pin_y: " + std::to_string(p.y));
    }
}

/// @brief Log a NaN partial for one pin, with the A/B/C terms it was formed from.
static void logNaNPartialDiagnostic(Net* net_p, const NetPin& pin, const Gradient& partial,
                                    Term& A_i, Term& B, Term& C, int net_size)
{
    Logger::log_error("NaN detected in partials for node " + pin.node_p->getName() +
                      " in net " + net_p->getName());
    Logger::log_error("partial x: " + std::to_string(partial.x) + " y: " + std::to_string(partial.y));
    Logger::log_error("net size: " + std::to_string(net_size));
    Logger::log_error(net_p->to_string());
    Logger::log_error("A: " + A_i.to_string());
    Logger::log_error("B: " + B.to_string());
    Logger::log_error("C: " + C.to_string());
}

/**
 * @brief Exact weighted-average HPWL gradient on the CPU — the golden reference the AIE
 *        and 'simple' backends are checked against. High-degree nets are masked out to
 *        match XPlace (see the net_mask note below).
 */
void Placer::computeHpwlPartials_CPU()
{
    TIME_FUNCTION();
    // Match XPlace net_mask (database.py:613, ignore_net_degree=100): high-degree nets
    // (clock/reset/scan spanning the die) are excluded from the wirelength gradient. Their
    // WA gradient pulls hundreds of unrelated cells and is noise for placement; XPlace drops
    // them from both the gradient and the HPWL metric (see computeTotalWirelength).
    int ignore_net_degree = cfg["params"]["ignore_net_degree"].value_or(100);
    for (Net* net_p : db.getNetsVector()) {
        const std::vector<NetPin>& pins = net_p->getPins();
        int net_size = net_p->getDegree();

        if (net_size <= 1 || net_size > ignore_net_degree) continue;

        // find max and min x and y pin positions (node + offset)
        float min_x = __FLT_MAX__, min_y = __FLT_MAX__, max_x = -__FLT_MAX__, max_y = -__FLT_MAX__;
        for (const NetPin& pin : pins) {
            Position p = pin.getProbePos();
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
        }

        // Compute A terms directly into our flat vector
        std::vector<Term> A(net_size);
        for (int i = 0; i < net_size; i++) {
            Position p = pins[i].getProbePos();
            A[i].plus.x  = exp((p.x - max_x) * inv_gamma);
            A[i].minus.x = exp((min_x - p.x) * inv_gamma);
            A[i].plus.y  = exp((p.y - max_y) * inv_gamma);
            A[i].minus.y = exp((min_y - p.y) * inv_gamma);
        }

        // Compute B and C terms
        Term B, C;
        B.clear(); C.clear();
        for (int i = 0; i < net_size; i++) {
            Position p = pins[i].getProbePos();
            B.plus.x  += A[i].plus.x;
            B.minus.x += A[i].minus.x;
            B.plus.y  += A[i].plus.y;
            B.minus.y += A[i].minus.y;
            C.plus.x  += A[i].plus.x  * p.x;
            C.minus.x += A[i].minus.x * p.x;
            C.plus.y  += A[i].plus.y  * p.y;
            C.minus.y += A[i].minus.y * p.y;
        }

        // Guard BEFORE dividing: a zero B term makes the reciprocals below inf/NaN, so the
        // diagnostic has to run first or it only reports damage already done (and the asserts
        // compile out under NDEBUG, leaving a silent NaN to propagate into the gradient).
        if(B.plus.x == 0 || B.minus.x == 0 || B.plus.y == 0 || B.minus.y == 0) {
            logZeroBTermDiagnostic(net_p, pins, A, B, min_x, max_x, min_y, max_y);
            // Same hard-divergence path as the NaN check below: flag and stop rather than
            // emitting inf gradients, so finalization still reports a best-so-far result.
            m_nan_detected = true;
            return;
        }

        // Pre-compute common terms
        float bpx_sq_inv = 1.0f / (B.plus.x * B.plus.x);
        float bmx_sq_inv = 1.0f / (B.minus.x * B.minus.x);
        float bpy_sq_inv = 1.0f / (B.plus.y * B.plus.y);
        float bmy_sq_inv = 1.0f / (B.minus.y * B.minus.y);

        // Compute partials and store — gradient accumulates onto parent node
        for (int i = 0; i < net_size; i++) {
            Position p = pins[i].getProbePos();

            Gradient partial;
            partial.x = ((1 + p.x * inv_gamma) * B.plus.x - (C.plus.x * inv_gamma))
                      * (A[i].plus.x * bpx_sq_inv)
                    - ((1 - p.x * inv_gamma) * B.minus.x + (C.minus.x * inv_gamma))
                      * (A[i].minus.x * bmx_sq_inv);

            partial.y = ((1 + p.y * inv_gamma) * B.plus.y - (C.plus.y * inv_gamma))
                      * (A[i].plus.y * bpy_sq_inv)
                    - ((1 - p.y * inv_gamma) * B.minus.y + (C.minus.y * inv_gamma))
                      * (A[i].minus.y * bmy_sq_inv);

            //check for NaNs
            if(partial.x != partial.x || partial.y != partial.y) {
                logNaNPartialDiagnostic(net_p, pins[i], partial, A[i], B, C, net_size);
                // Hard divergence: flag it and stop computing partials rather than exit(1).
                // run() breaks on m_nan_detected so finalization restores the best-so-far placement
                // and still writes a results row (a DSE sweep no longer loses the run).
                m_nan_detected = true;
                return;
            }

            pins[i].node_p->next.probe_grad += partial;
        }
    }
}

AIEPLACE_NAMESPACE_END
