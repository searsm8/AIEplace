#ifndef PL_ALGO_BB_REDUCE_HPP
#define PL_ALGO_BB_REDUCE_HPP

// bb_reduce -- the Barzilai-Borwein step-length reduction, on the PL.
//
// Computes the two scalar norms that param_scheduler turns into the BB step alpha:
//   pos_norm_sq  = sum_n ||v_n - v_prev_n||^2                 (probe displacement)
//   grad_norm_sq = sum_n ||(g_total_n - g_total_prev_n)/precond_n||^2   (preconditioned grad delta)
// It also MATERIALIZES this iteration's combined gradient g_total = g_hpwl - lambda*g_density into
// g_total_out, which becomes g_total_prev for the next iteration's difference. This is the on-device
// replacement for the host BB reduction (sw_only computeLipshitzEstimate, AIEplace.cpp:281) and for
// host Placement.hpp bbStepLength -- removing the last per-iteration host round-trip in the control
// path, which is what makes the device-resident loop possible.
//
// Position/gradient convention matches iteration_update: v = probe position (node_box.{x,y}),
// g_total the combined gradient at v. The preconditioned delta must match iteration_update's step
// (which divides the combined gradient by precond), so the same 1/precond is applied here -- exactly
// the raw-vs-preconditioned consistency fix in the golden (commit b20a2cc). precond is 1.0 when
// preconditioning is off, leaving grad_norm_sq the raw combined-gradient delta.
//
// Sums accumulate in float (matches the golden, which sums in float); the two scalars are read by
// param_scheduler. Movable nodes only ([0,M)).

#include "../formats.hpp"
#include "../host_interface.hpp"

namespace plalgo {

static void bb_reduce(
    const coord_t* v_cur,        // [M] this iteration's probe positions v_{k+1}
    const coord_t* v_prev,       // [M] previous iteration's probe positions v_k
    const coord_t* g_hpwl,       // [M] HPWL gradient at v_{k+1}
    const coord_t* g_density,    // [M] density gradient at v_{k+1}
    const coord_t* g_total_prev, // [M] combined gradient at v_k (stored last iteration)
    const float*   precond,      // [M] diagonal preconditioner weight (>= 1; 1.0 when precond off)
    float          lambda,       // density weight in use this iteration
    int            num_movable,
    coord_t*       g_total_out,   // [M] combined gradient at v_{k+1} (becomes g_total_prev next iter)
    float*         pos_norm_sq,   // out scalar
    float*         grad_norm_sq)  // out scalar
{
    float pos = 0.0f;
    float grad = 0.0f;
bb_loop:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE
        // ||v_{k+1} - v_k||^2
        const float dvx = v_cur[n].x - v_prev[n].x;
        const float dvy = v_cur[n].y - v_prev[n].y;
        pos += dvx * dvx + dvy * dvy;
        // combined gradient at v_{k+1}: g_total = g_wl - lambda*g_density (sign per the 5c audit)
        const float gx = g_hpwl[n].x - lambda * g_density[n].x;
        const float gy = g_hpwl[n].y - lambda * g_density[n].y;
        // preconditioned gradient delta (must match iteration_update's stepped map)
        const float inv_p = 1.0f / precond[n];
        const float dgx = inv_p * (gx - g_total_prev[n].x);
        const float dgy = inv_p * (gy - g_total_prev[n].y);
        grad += dgx * dgx + dgy * dgy;
        coord_t gt; gt.x = gx; gt.y = gy;
        g_total_out[n] = gt;     // materialize for the next iteration's difference
    }
    *pos_norm_sq  = pos;
    *grad_norm_sq = grad;
}

} // namespace plalgo

#endif // PL_ALGO_BB_REDUCE_HPP
