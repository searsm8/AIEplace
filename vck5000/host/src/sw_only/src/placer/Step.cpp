/**
 * @file Step.cpp
 * @brief Nesterov/Barzilai-Borwein step machinery: gradient combination, boundary
 *        enforcement, the BB step-length estimate, and the backtracked step itself.
 *        Split out of AIEplace.cpp.
 */

#include "AIEplace.h"
#include <cmath>
#include <algorithm>

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief Estimate ᾱ_k from current positions vs stored prev-iteration data.
 *
 * Reads: node positions, prev_lookahead_pos (v_{k-1}), HPWL partials ∇f_pre(v_k),
 *        prev_lookahead_grad (∇f_pre(v_{k-1})).
 * Does NOT modify any state — call updateBBState() after the committed nudge.
 *
 * @return Raw Barzilai-Borwein step estimate ‖Δv‖/‖Δg‖ (no clamp — matches XPlace).
 */
float Placer::computeLipschitzEstimate()
{
    TIME_FUNCTION();
    float pos_norm_sq  = 0.0f;
    float grad_norm_sq = 0.0f;
    const auto& nodes = db.getMovableNodes();

    m_ordered_reduce.sum2((int)nodes.size(),
        [&](int i, float& pos_term, float& grad_term) {
            Node* node_p = nodes[i];
            // ||v̂_{k+1} - v_k||²
            float dx = node_p->next.probe_pos.x - node_p->current.probe_pos.x;
            float dy = node_p->next.probe_pos.y - node_p->current.probe_pos.y;
            pos_term = dx*dx + dy*dy;

            // ||∇f(v̂_{k+1}) - ∇f(v_k)||² of the PRECONDITIONED gradient — must match Node::step,
            // which moves by (1/precond_weight)·grad. The BB estimate α=‖Δv‖/‖Δg‖ is only valid when
            // Δg is the same map that is stepped; using the raw gradient here makes α too small when
            // preconditioning is on (P≥1 ⇒ raw grad larger), starving the step so density never spreads.
            // precond_weight is fixed within the iteration, so dividing the difference is exact; it is
            // 1.0 when preconditioning is off, leaving that path unchanged.
            float inv_pw = 1.0f / node_p->precond_weight;
            float dgx = inv_pw * (node_p->next.probe_grad.x - node_p->current.probe_grad.x);
            float dgy = inv_pw * (node_p->next.probe_grad.y - node_p->current.probe_grad.y);
            grad_term = dgx*dgx + dgy*dgy;
        }, pos_norm_sq, grad_norm_sq);

    float estimate = sqrtf(pos_norm_sq) / sqrtf(grad_norm_sq + 1e-8f);
    Logger::log_detail("New steplength estimate: " + PREC_P(estimate, 4));
    // No magnitude clamp — mirrors XPlace (nesterov_optimizer.py), which uses the raw
    // Barzilai-Borwein ratio ‖Δv‖/‖Δg‖ and relies solely on the backtracking line search to
    // reject over-aggressive steps. The estimate already self-scales with preconditioning because
    // Δg is the preconditioned gradient difference, so no precond-dependent cap is needed.
    return estimate;
}


/**
 * @brief Subtract electrostatic force from probe_grad in-place to form total gradient.
 *
 * Called after computeAllPartials() which accumulates HPWL into next.probe_grad.
 * For components: next.probe_grad -= electro (electro already includes λ weighting)
 * For fillers: next.probe_grad = -electro (no HPWL partials)
 */
void Placer::combineGradients()
{
    TIME_FUNCTION();
    // Refresh the committed-gradient L1 norms while both force components are in hand:
    // last_gwl_L1 = Σ‖∇wl‖₁ (probe_grad before the subtraction), last_gden_L1 = Σ‖λ·∇den‖₁ (the
    // electrostatic force, which already carries λ). The force-ratio dff (next iteration) reads these.
    // Each node's gradient update touches only that node, so the loop itself is bit-exact
    // threaded; the two L1 norms are the only reduction, and OrderedReduce keeps them in node
    // order whatever the thread count (cheap here — the force gather dominates the per-node cost).
    const auto& nodes = db.getMovableNodes();
    const int filler_start = db.getFillerStartIndex();

    m_ordered_reduce.sum2((int)nodes.size(),
        [&](int i, float& gwl_term, float& gden_term) {
            Gradient& g = nodes[i]->next.probe_grad;
            // fillers are on no nets, so they contribute no wirelength gradient
            gwl_term = (i < filler_start) ? fabsf(g.x) + fabsf(g.y) : 0.0f;
            Gradient electro = computeElectrostaticForce(nodes[i]);
            gden_term = fabsf(electro.x) + fabsf(electro.y);
            g -= electro;
        }, last_gwl_L1, last_gden_L1);
}


/**
 * @brief Clamp node positions so the cell stays within the die area.
 *
 * The node position is the lower-left corner of the cell, so the upper bound must account for the
 * cell's width/height to prevent the right/top edge from extending past the die boundary.
 *
 * Two modes (TODO #11a A/B):
 *  - legacy (default): bound the RAW cell, [0, die - size]. The sqrt(2)-expanded density footprint
 *    can then still hang off the edge, and computeNodeFootprint shifts it back at deposit time.
 *  - xplace_die_projection: bound the cell so the EXPANDED footprint is in-die, which is XPlace's
 *    trunc_node_pos_fn (run_placement_nesterov.py:5-11) re-applied on every gradient evaluation.
 *    The deposit-time shift is then disabled (Grid::setShiftFootprintInDie) — the position is
 *    already legal, so the deposited mass stays centered on the cell instead of sliding off it.
 */
void Placer::enforceDieBoundaries(Node* node_p)
{
    const float die_w = (float)grid.getDieWidth();
    const float die_h = (float)grid.getDieHeight();
    const float w = node_p->getXsize();
    const float h = node_p->getYsize();

    float min_x = 0.0f,          min_y = 0.0f;
    float max_x = die_w - w,     max_y = die_h - h;

    if (xplace_die_projection) {
        // XPlace bounds the CENTRE to [expanded/2, die - expanded/2]. Our position is the lower-left
        // (centre = pos + raw/2), so the equivalent bound on the lower-left is
        //     [ (cw - w)/2 , die_w - (cw + w)/2 ]
        // which collapses to the legacy [0, die_w - w] exactly when cw == w (macros, clamp off).
        float cw = w, ch = h;
        if (enable_density_clamp) {
            cw = std::max(w, grid.getBinWidth()  * (float)M_SQRT2);
            ch = std::max(h, grid.getBinHeight() * (float)M_SQRT2);
        }
        min_x = 0.5f * (cw - w);   max_x = die_w - 0.5f * (cw + w);
        min_y = 0.5f * (ch - h);   max_y = die_h - 0.5f * (ch + h);
        // A footprint wider than the die would invert the interval (UB in std::clamp) — centre it.
        if (min_x > max_x) min_x = max_x = 0.5f * (die_w - w);
        if (min_y > max_y) min_y = max_y = 0.5f * (die_h - h);
    }

    // Clamp node_pos
    node_p->next.node_pos.x = std::clamp(node_p->next.node_pos.x, min_x, max_x);
    node_p->next.node_pos.y = std::clamp(node_p->next.node_pos.y, min_y, max_y);

    // Clamp probe_pos
    node_p->next.probe_pos.x = std::clamp(node_p->next.probe_pos.x, min_x, max_x);
    node_p->next.probe_pos.y = std::clamp(node_p->next.probe_pos.y, min_y, max_y);
}


/**
 * @brief Save current positions and gradients as "previous" for BB estimate and backtracking.
 * Must be called before stepAllNodes() so prev fields serve as the restore point.
 */
void Placer::advanceIterationState()
{
    const auto& nodes = db.getMovableNodes();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)nodes.size(); i++) nodes[i]->cacheState();
}

/**
 * @brief Perform Nesterov gradient step for all nodes (Algorithm 1, Lines 2 & 4).
 *
 * Delegates to Node::step() which reads from current state and writes to next.
 * The momentum coefficient is computed by the caller (computeNextStep owns Nesterov state).
 *
 * @param mom_coeff Momentum coefficient: (a_k - 1) / a_{k+1}, or 0 if momentum disabled.
 */
void Placer::stepAllNodes()
{
    TIME_FUNCTION();
    const auto& nodes = db.getMovableNodes();

    // Each node's step reads and writes only its own state, so this is bit-exact threaded.
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)nodes.size(); i++) {
        nodes[i]->step(step_length, momentum_coeff);
        enforceDieBoundaries(nodes[i]);
    }
}

/**
 * @brief XPlace-style initial learning-rate (step-length) estimate, run once at iteration 1.
 *
 * Mirrors Xplace estimate_initial_learning_rate (initializer.py:171): from the current movable
 * positions x0 with total gradient g0, take ONE trial step x' = x0 − seed·P·g0
 * (seed = init_step_seed, XPlace args.lr), recompute the total gradient g' there, and set
 *     step_length = ‖x' − x0‖₂ / ‖P·(g' − g0)‖₂        (Barzilai-Borwein inverse-Lipschitz)
 * which is exactly the ratio computeLipschitzEstimate() forms. The step (Node::step) and the
 * estimate both divide by precond_weight, so the whole ratio is in the preconditioned gradient
 * — matching XPlace, whose calc_obj_and_grad returns the preconditioned gradient.
 *
 * Positions are restored to x0 afterwards and next.probe_grad left = g0 (the total gradient), so
 * the real first step in performNextStep starts from x0 exactly as every later iteration does —
 * the only lasting effect is the calibrated step_length. Costs one extra gradient evaluation, once.
 *
 * Preconditions: density_weight (initializeDensityWeight) and precond_weight (updatePrecondWeights)
 * are set; next holds x0 with the HPWL-only gradient in probe_grad.
 */
void Placer::estimateInitialStep()
{
    // g0 = total gradient at x0 (subtract λ·density force from the HPWL-only next.probe_grad).
    combineGradients();

    // Snapshot the anchor (x0, g0) into current so computeLipschitzEstimate can diff against it.
    advanceIterationState();

    // One trial step x' = x0 − seed·P·g0, momentum off (matches XPlace's x_k − lr·g_k).
    step_length    = init_step_seed;
    momentum_coeff = 0.0f;
    stepAllNodes();

    // g' = total gradient at the trial point x'.
    iterationReset();
    computeHpwlPartials();
    computeElectricFields();
    combineGradients();

    // Barzilai-Borwein estimate α = ‖Δv‖₂ / ‖P·Δg‖₂ (no magnitude clamp — as in XPlace).
    step_length = computeLipschitzEstimate();
    Logger::log_detail("Estimated initial step_length (BB): " + PREC_P(step_length, 6) +
                     "  (seed " + PREC_P(init_step_seed, 4) + ")");

    // Undo the probe step: restore x0 / g0 into next so the real first step starts from x0.
    const auto& nodes = db.getMovableNodes();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)nodes.size(); i++) nodes[i]->restoreState();
}


/**
 * @brief Algorithm 2 (BkTrk): Backtracking line search for step length.
 *
 * Uses the Barzilai-Borwein (Lipschitz) estimate to validate the step length.
 * The do-while loop takes trial steps with the current step_length, recomputes
 * gradients at the trial position, and checks if α̂ ≤ ε · fresh_bb.
 * If rejected, positions are restored and step_length is updated to the fresh estimate.
 *
 * After the loop, the accepted trial step is committed (positions already at u_{k+1}, v_{k+1})
 * and the Nesterov coefficient is advanced.
 */
void Placer::performNextStep(bool backtracking_enabled)
{
    // Algorithm 1, Line 3: compute momentum coefficient for this iteration
    float a_next = (1.0f + sqrtf(4.0f * nesterov_ak * nesterov_ak + 1.0f)) / 2.0f;
    momentum_coeff = enable_momentum ? (nesterov_ak - 1.0f) / a_next : 0.0f;
    nesterov_ak = a_next;

    // step_length (α̂) carries over from previous iteration (or warmup default)
    advanceIterationState();  // copy next state into current state

    // Algorithm 2: Backtracking
    int tries = 0;
    float prev_step_length;
    do {
        // Lines 2 & 3: trial step using existing step_length from previous iteration
        stepAllNodes();

        iterationReset();
        // Recompute gradients at trial v̂ (the expensive part, accelerated on AIEs)
        computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
        computeElectricFields();    // ∇D from ρ → bin eFields
        combineGradients();         // add electro in-place: next.probe_grad becomes total ∇f

        prev_step_length = step_length;

        // new steplength estimate at probe position:
        // α = 1 / L = ||v̂ - v_k|| / ||∇f(v̂) - ∇f(v_k)||
        step_length = computeLipschitzEstimate();

        // Accept if α̂ ≤ ε · fresh_bb (step is not too aggressive)
    } while(backtracking_enabled &&
            prev_step_length > backtrack_epsilon * step_length && // epsilon condition
            ++tries < max_backtracking_attempts);

    backtrack_steps = tries; // for logging

    if (Logger::isLevelActive(LogLevel::DEBUG)) logStepDiagnostics();
}

/// @brief Log per-iteration step diagnostics (gradient norms, step length, overflow) — DEBUG only.
void Placer::logStepDiagnostics()
{
    Logger::log_debug("=== Step Diagnostics (iteration " + std::to_string(iteration) + ") ===");
    Logger::log_debug("  step_length (α̂):  " + PREC_P(step_length, 6));
    Logger::log_debug("  momentum_coeff:    " + PREC_P(momentum_coeff, 6));
    Logger::log_debug("  nesterov_ak:       " + PREC_P(nesterov_ak, 6));
    Logger::log_debug("  backtrack_steps:   " + std::to_string(backtrack_steps));

    // Gradient statistics (from current state used for stepping)
    float grad_L1 = 0.0f, max_grad = 0.0f;
    for (const auto& item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* n = item.second;
        grad_L1 += fabs(n->current.probe_grad.x) + fabs(n->current.probe_grad.y);
        max_grad = std::max(max_grad, std::max(fabs(n->current.probe_grad.x), fabs(n->current.probe_grad.y)));
    }
    Logger::log_debug("  grad L1 norm:      " + SCI(grad_L1));
    Logger::log_debug("  max |grad|:        " + SCI(max_grad));
    Logger::log_debug("  α̂ * max|grad|:     " + SCI(step_length * max_grad) + "  (max single-step displacement)");

    // Position delta statistics
    float max_node_delta = 0.0f, max_probe_overshoot = 0.0f;
    int clamped_count = 0;
    float die_w = (float)grid.getDieWidth(), die_h = (float)grid.getDieHeight();
    for (const auto& item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* n = item.second;
        float dx = fabs(n->next.node_pos.x - n->current.node_pos.x);
        float dy = fabs(n->next.node_pos.y - n->current.node_pos.y);
        max_node_delta = std::max(max_node_delta, std::max(dx, dy));

        float ox = fabs(n->next.probe_pos.x - n->next.node_pos.x);
        float oy = fabs(n->next.probe_pos.y - n->next.node_pos.y);
        max_probe_overshoot = std::max(max_probe_overshoot, std::max(ox, oy));

        bool at_boundary = (n->next.node_pos.x <= 0 || n->next.node_pos.x >= die_w ||
                           n->next.node_pos.y <= 0 || n->next.node_pos.y >= die_h);
        if (at_boundary)
            clamped_count++;
    }
    Logger::log_debug("  max |Δnode_pos|:   " + SCI(max_node_delta));
    Logger::log_debug("  max probe overshoot: " + SCI(max_probe_overshoot));
    Logger::log_debug("  nodes at boundary: " + std::to_string(clamped_count));

    // Sample trace: first 3 components
    int count = 0;
    for (const auto& item : db.getComponents()) {
        if (count >= 3) break;
        if (item.second->getStatus() == FIXED) continue;
        Node* n = item.second;
        Logger::log_debug("  --- Sample node: " + n->getName() + " ---");
        Logger::log_debug("    current.node_pos (u_k):   (" + PREC_P(n->current.node_pos.x, 2) + ", " + PREC_P(n->current.node_pos.y, 2) + ")");
        Logger::log_debug("    current.probe_pos (v_k):  (" + PREC_P(n->current.probe_pos.x, 2) + ", " + PREC_P(n->current.probe_pos.y, 2) + ")");
        Logger::log_debug("    current.probe_grad:       (" + SCI(n->current.probe_grad.x) + ", " + SCI(n->current.probe_grad.y) + ")");
        Logger::log_debug("    α̂ * grad:                 (" + SCI(step_length * n->current.probe_grad.x) + ", " + SCI(step_length * n->current.probe_grad.y) + ")");
        Logger::log_debug("    next.node_pos (u_{k+1}):  (" + PREC_P(n->next.node_pos.x, 2) + ", " + PREC_P(n->next.node_pos.y, 2) + ")");
        Logger::log_debug("    next.probe_pos (v_{k+1}): (" + PREC_P(n->next.probe_pos.x, 2) + ", " + PREC_P(n->next.probe_pos.y, 2) + ")");
        Logger::log_debug("    next.probe_grad:          (" + SCI(n->next.probe_grad.x) + ", " + SCI(n->next.probe_grad.y) + ")");
        count++;
    }
    Logger::log_debug("=== End Step Diagnostics ===");
}

AIEPLACE_NAMESPACE_END
