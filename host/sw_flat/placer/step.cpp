// step.cpp -- sw_only's Step.cpp without backtracking (D6): the Nesterov step, the iteration-1
// Barzilai-Borwein step estimate, gradient combination, and the expanded-size die bound. Meow.

#include "placer.hpp"

#include "modules/iteration_update/slot_reduce.hpp"   // reductions = "lanes": the device's summation order

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sw_flat {

// performNextStep with enable_backtracking = false, up to the move: one trial, always accepted. The
// gradient at the new point is the next iteration's (Placer::run), and so is the BB step from it. Meow.
void Placer::take_step()
{
    float a_next = (1.0f + sqrtf(4.0f * nesterov_ak * nesterov_ak + 1.0f)) / 2.0f;
    momentum_coeff = cfg.enable_momentum ? (nesterov_ak - 1.0f) / a_next : 0.0f;
    nesterov_ak = a_next;

    cache_state();
    step_all_nodes();
    send_borrowed_positions();
}

// XPlace estimate_initial_learning_rate (initializer.py:171): one trial step of init_step_seed site
// widths, then step_length = |dx| / |P dg|; the positions go back to x0. Meow.
void Placer::estimate_initial_step()
{
    combine_gradients();
    cache_state();

    const float site_width = design.site_width > 0.0f ? design.site_width : design.row_height;
    if (site_width <= 0.0f) { fprintf(stderr, "sw_flat: the design has neither a site width nor a row height\n"); exit(1); }
    step_length    = cfg.init_step_seed * site_width;
    momentum_coeff = 0.0f;
    step_all_nodes();
    send_borrowed_positions();

    compute_hpwl_partials();
    compute_electric_fields();
    merge_borrowed_gradients();
    combine_gradients();

    step_length = lipschitz_estimate();
    if (!(step_length > 0.0f)) {
        fprintf(stderr, "sw_flat: initial BB step estimate is %g: raise init_step_seed (TODO #23)\n", step_length);
        exit(1);
    }
    restore_state();
}

// computeLipschitzEstimate: |v_next - v|^2 and |P (g_next - g)|^2, summed in reduce_slots order. Meow.
float Placer::lipschitz_estimate()
{
    float pos_norm_sq = 0.0f, grad_norm_sq = 0.0f;
    std::vector<float> pos_terms(reduce_slots.size()), grad_terms(reduce_slots.size());
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)reduce_slots.size(); i++) {
        const long slot = reduce_slots[i];
        float dx = next.probe_pos[slot].x - current.probe_pos[slot].x;
        float dy = next.probe_pos[slot].y - current.probe_pos[slot].y;
        pos_terms[i] = dx*dx + dy*dy;
        float inv_pw = 1.0f / precond_weight[slot];
        float dgx = inv_pw * (next.probe_grad[slot].x - current.probe_grad[slot].x);
        float dgy = inv_pw * (next.probe_grad[slot].y - current.probe_grad[slot].y);
        grad_terms[i] = dgx*dgx + dgy*dgy;
    }
    pos_norm_sq = reduce_sum(pos_terms);
    grad_norm_sq = reduce_sum(grad_terms);

    last_pos_norm_sq = pos_norm_sq; last_grad_norm_sq = grad_norm_sq;
    return sqrtf(pos_norm_sq) / sqrtf(grad_norm_sq + 1e-8f);
}

// combineGradients: probe_grad -= lambda * density force, and the two L1 norms in reduce_slots order. Meow.
void Placer::combine_gradients()
{
    PhaseTimer timer{time_combine};
    std::vector<float> gwl_terms(reduce_slots.size()), gden_terms(reduce_slots.size());
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)reduce_slots.size(); i++) {
        const long slot = reduce_slots[i];
        Gradient& g = next.probe_grad[slot];
        gwl_terms[i] = (i < filler_start) ? fabsf(g.x) + fabsf(g.y) : 0.0f;
        Gradient electro = electrostatic_force(slot);
        gden_terms[i] = fabsf(electro.x) + fabsf(electro.y);
        g -= electro;
    }
    last_gwl_L1 = reduce_sum(gwl_terms);
    last_gden_L1 = reduce_sum(gden_terms);
}

float Placer::reduce_sum(const std::vector<float>& terms) const
{
    if (!cfg.reductions_lanes) {
        float sum = 0.0f;
        for (float term : terms) sum += term;
        return sum;
    }
    std::vector<float> by_slot(layout.num_slots, 0.0f);   // a slot that is not movable sums as the device's masked 0
    for (size_t i = 0; i < reduce_slots.size(); i++) by_slot[reduce_slots[i]] = terms[i];
    plalgo::SlotReducer reducer;
    reducer.clear();
    for (size_t b = 0; b < lane_beats.size(); b++) {
        float lanes[pinrec::LANES];
        for (int j = 0; j < pinrec::LANES; j++) lanes[j] = by_slot[lane_beats[b] * pinrec::LANES + j];
        reducer.add_beat((int)b, lanes);
    }
    return reducer.total();
}

// Node::step, then enforceDieBoundaries. Meow.
void Placer::step_all_nodes()
{
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)movable_slots.size(); i++) {
        const long slot = movable_slots[i];
        Gradient precond_grad = (1.0f / precond_weight[slot]) * current.probe_grad[slot];
        next.node_pos[slot] = current.probe_pos[slot] - step_length * precond_grad;
        next.probe_pos[slot] = next.node_pos[slot] + momentum_coeff * (next.node_pos[slot] - current.node_pos[slot]);
        enforce_die_boundaries(slot);
    }
}

// XPlace's trunc_node_pos_fn on the lower-left: the sqrt(2)-EXPANDED footprint stays in the die. Meow.
void Placer::enforce_die_boundaries(long slot)
{
    const float die_wf = (float)die_w;
    const float die_hf = (float)die_h;
    const float w = layout.width[slot];
    const float h = layout.height[slot];
    float cw = w, ch = h;
    if (cfg.enable_density_clamp) {
        cw = std::max(w, bin_w * (float)M_SQRT2);
        ch = std::max(h, bin_h * (float)M_SQRT2);
    }
    float min_x = 0.5f * (cw - w), max_x = die_wf - 0.5f * (cw + w);
    float min_y = 0.5f * (ch - h), max_y = die_hf - 0.5f * (ch + h);
    if (min_x > max_x) min_x = max_x = 0.5f * (die_wf - w);
    if (min_y > max_y) min_y = max_y = 0.5f * (die_hf - h);

    next.node_pos[slot].x  = std::clamp(next.node_pos[slot].x, min_x, max_x);
    next.node_pos[slot].y  = std::clamp(next.node_pos[slot].y, min_y, max_y);
    next.probe_pos[slot].x = std::clamp(next.probe_pos[slot].x, min_x, max_x);
    next.probe_pos[slot].y = std::clamp(next.probe_pos[slot].y, min_y, max_y);
}

void Placer::cache_state()
{
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)movable_slots.size(); i++) {
        const long slot = movable_slots[i];
        current.node_pos[slot] = next.node_pos[slot];
        current.probe_pos[slot] = next.probe_pos[slot];
        current.probe_grad[slot] = next.probe_grad[slot];
    }
}

void Placer::restore_state()
{
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)movable_slots.size(); i++) {
        const long slot = movable_slots[i];
        next.node_pos[slot] = current.node_pos[slot];
        next.probe_pos[slot] = current.probe_pos[slot];
        next.probe_grad[slot] = current.probe_grad[slot];
    }
}

} // namespace sw_flat
