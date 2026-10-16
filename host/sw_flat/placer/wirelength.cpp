// wirelength.cpp -- sw_only's Partials.cpp (computeHpwlPartials_CPU, the exact WA gradient) and the
// HPWL metric (DataBase::computeTotalWirelength). Nets of degree 2..ignore_net_degree only (XPlace's
// net_mask). Meow.

#include "placer.hpp"

#include <algorithm>
#include <cmath>

namespace sw_flat {

// Per net in parallel into pin_partials, then replayed serially in file order (net, then pin):
// sw_only's deterministic path, so every slot's probe_grad rounds identically. [sw_flat] wa_sums and
// grad_merge swap either half for the PL's (record_stream.cpp). Meow.
void Placer::compute_hpwl_partials()
{
    PhaseTimer timer{time_gradient};
    for (long slot : movable_slots) next.probe_grad[slot].clear();
    if (cfg.wa_sums_tree) compute_stream_partials();
    else compute_net_partials();
    if (nan_detected) return;
    if (cfg.grad_merge_stream) { merge_stream_gradient(); return; }
    if (cfg.wa_sums_tree)
        for (size_t lane = 0; lane < stream.lane_pin.size(); lane++)
            if (stream.lane_pin[lane] >= 0) pin_partials[stream.lane_pin[lane]] = lane_grad[lane];

    for (int net : gradient_nets) {
        const int first = design.net_first_pin[net];
        for (int i = 0; i < design.net_degree(net); i++)
            if (pin_slot[first + i] >= 0) next.probe_grad[pin_slot[first + i]] += pin_partials[first + i];
    }
}

void Placer::compute_net_partials()
{
    const bool use_lut = cfg.wa_exp_lut;
    const float inv_lut_step = 1.0f / (PL_LUT_STEP_NORM * gamma);   // the pl_algo host's

    #pragma omp parallel for schedule(dynamic, 256)
    for (int k = 0; k < (int)gradient_nets.size(); k++) {
        if (nan_detected) continue;
        const int net = gradient_nets[k];
        const int first = design.net_first_pin[net];
        const int net_size = design.net_degree(net);

        float min_x = __FLT_MAX__, min_y = __FLT_MAX__, max_x = -__FLT_MAX__, max_y = -__FLT_MAX__;
        for (int i = 0; i < net_size; i++) {
            Position p = pin_position(*this, first + i, true);
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
        }

        static thread_local std::vector<AIEplace::Term> A;
        if ((int)A.size() < net_size) A.resize(net_size);
        for (int i = 0; i < net_size; i++) {
            Position p = pin_position(*this, first + i, true);
            if (use_lut) {
                A[i].plus.x  = lut_exp(exp_lut, inv_lut_step, max_x - p.x);
                A[i].minus.x = lut_exp(exp_lut, inv_lut_step, p.x - min_x);
                A[i].plus.y  = lut_exp(exp_lut, inv_lut_step, max_y - p.y);
                A[i].minus.y = lut_exp(exp_lut, inv_lut_step, p.y - min_y);
            } else {
                A[i].plus.x  = std::exp((p.x - max_x) * inv_gamma);
                A[i].minus.x = std::exp((min_x - p.x) * inv_gamma);
                A[i].plus.y  = std::exp((p.y - max_y) * inv_gamma);
                A[i].minus.y = std::exp((min_y - p.y) * inv_gamma);
            }
        }

        AIEplace::Term B, C;
        B.clear(); C.clear();
        for (int i = 0; i < net_size; i++) {
            Position p = pin_position(*this, first + i, true);
            B.plus.x  += A[i].plus.x;
            B.minus.x += A[i].minus.x;
            B.plus.y  += A[i].plus.y;
            B.minus.y += A[i].minus.y;
            C.plus.x  += A[i].plus.x  * p.x;
            C.minus.x += A[i].minus.x * p.x;
            C.plus.y  += A[i].plus.y  * p.y;
            C.minus.y += A[i].minus.y * p.y;
        }
        if (B.plus.x == 0 || B.minus.x == 0 || B.plus.y == 0 || B.minus.y == 0) {
            nan_detected = true;
            continue;
        }

        float bpx_sq_inv = 1.0f / (B.plus.x * B.plus.x);
        float bmx_sq_inv = 1.0f / (B.minus.x * B.minus.x);
        float bpy_sq_inv = 1.0f / (B.plus.y * B.plus.y);
        float bmy_sq_inv = 1.0f / (B.minus.y * B.minus.y);

        for (int i = 0; i < net_size; i++) {
            Position p = pin_position(*this, first + i, true);
            Gradient partial;
            partial.x = ((1 + p.x * inv_gamma) * B.plus.x - (C.plus.x * inv_gamma))
                      * (A[i].plus.x * bpx_sq_inv)
                    - ((1 - p.x * inv_gamma) * B.minus.x + (C.minus.x * inv_gamma))
                      * (A[i].minus.x * bmx_sq_inv);
            partial.y = ((1 + p.y * inv_gamma) * B.plus.y - (C.plus.y * inv_gamma))
                      * (A[i].plus.y * bpy_sq_inv)
                    - ((1 - p.y * inv_gamma) * B.minus.y + (C.minus.y * inv_gamma))
                      * (A[i].minus.y * bmy_sq_inv);
            if (partial.x != partial.x || partial.y != partial.y) {
                nan_detected = true;
                break;
            }
            pin_partials[first + i] = partial;
        }
    }
}

// Net::computeWirelength_HPWL summed in net-name order, in double like DataBase::computeTotalWirelength;
// a masked net adds +0.0f (exact). Degree 1 is NOT masked here (its HPWL is 0), unlike the gradient. Meow.
float Placer::total_wirelength(int max_net_degree, bool at_probe) const
{
    double total = 0.0;
    for (int net : nets_by_name) {
        const int degree = design.net_degree(net);
        if (degree == 0 || degree > max_net_degree) { total += 0.0f; continue; }
        const int first = design.net_first_pin[net];
        Position first_pos = pin_position(*this, first, at_probe);
        float min_x = first_pos.x, max_x = first_pos.x;
        float min_y = first_pos.y, max_y = first_pos.y;
        for (int i = 0; i < degree; i++) {
            Position p = pin_position(*this, first + i, at_probe);
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
        }
        total += (max_x - min_x) + (max_y - min_y);
    }
    return (float)total;
}

} // namespace sw_flat
