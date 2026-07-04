#ifndef PL_ALGO_ITERATION_UPDATE_HPP
#define PL_ALGO_ITERATION_UPDATE_HPP

// iteration_update -- Stage 5c: the ePlace position update for the movable nodes.
//
// One call = one Nesterov step. It mirrors, exactly, three markv1 functions run
// back-to-back (host/src/markv1/src/AIEplace.cpp, include/Node.h):
//   combineGradients()   : g_total = g_wl - lambda * g_density          (in-place -=)
//   Node::step()         : precondition, then u_{k+1} = v_k - alpha*P*g_total,
//                          then v_{k+1} = u_{k+1} + coeff*(u_{k+1} - u_k)
//   enforceDieBoundaries : clamp BOTH u_{k+1} and v_{k+1} into [0, die - size]
// See CHECKPOINT.md "Stage 5c plan" and the 5c algorithm audit (memory
// pl_algo_5c_algo_audit) for why the sign is `-` (the eField sign convention bakes
// Xplace's `+=` into the field) and why there is no per-bin local_density_weight.
//
// Position convention (matches markv1 State):
//   u = committed "node" position; v = look-ahead "probe" position. The gradient
//   pipeline (hpwl_CU, density solve) is evaluated at v, so v_k is the step anchor
//   and node_box carries it (node_box[n].{x,y} == v_k, {w,h} == cell size). u_k is a
//   separate committed buffer. This call emits u_{k+1} (owned Nesterov state) and
//   STREAMS v_{k+1} to the Memory Writer, which is the single writer of the canonical
//   coords buffer that the next iteration's gradient pipeline reads.
//
// Preconditioner: the diagonal weight P^-1 = 1/precond_weight[n] is supplied per node
// by the host (host owns the lambda / precond_coef schedule for v1), so this module
// just divides. lambda, alpha (BB step) and coeff (Nesterov momentum) are scalars from
// the host policy. Movable nodes only ([0,M)); fixed nodes never move.
//
// v1 is a straightforward per-node pass (natural coord_t pointers, like force_gather).
// It is the producer half of a DATAFLOW pair with memory_writer (top.cpp wires them).

#include "../formats.hpp"
#include "../host_interface.hpp"

namespace plalgo {

// Clamp to [lo, hi] (HLS-friendly; std::clamp pulls <algorithm> into the datapath).
static inline float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

static void iteration_update(
    const coord_t* g_hpwl,     // [M] HPWL gradient at v_k        (node_grad, gmem7)
    const coord_t* g_density,  // [M] density gradient at v_k     (dct_in,    gmem10)
    const NodeBox* node_box,   // [M] {x,y} = v_k anchor, {w,h} = cell size (gmem8)
    const coord_t* u_in,       // [M] u_k committed position      (node_pos,  gmem0)
    const float*   precond,    // [M] preconditioner weight       (exp_lut,   gmem4)
    coord_t*       u_out,      // [M] u_{k+1} committed position   (dct_out,   gmem11)
    float lambda,              // density weight (host schedule)
    float alpha,               // Barzilai-Borwein step length (host)
    float coeff,               // Nesterov momentum coeff (a_k-1)/a_{k+1} (host; 0 in warmup)
    float die_xmax,            // die width  (clamp upper bound is die_xmax - w)
    float die_ymax,            // die height (clamp upper bound is die_ymax - h)
    int   num_movable,
    hls::stream<coord_t>& v_out)  // v_{k+1} stream -> memory_writer
{
node_loop:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE
        // combine: g_total = g_wl - lambda * g_density  (sign per audit; local_dw dropped)
        const float gx = g_hpwl[n].x - lambda * g_density[n].x;
        const float gy = g_hpwl[n].y - lambda * g_density[n].y;
        // precondition: divide the COMBINED gradient by the diagonal weight (>= 1)
        const float inv_p = 1.0f / precond[n];
        const float pgx = gx * inv_p;
        const float pgy = gy * inv_p;
        // BB step from the probe anchor v_k:  u_{k+1} = v_k - alpha * P * g_total
        const float vkx = node_box[n].x, vky = node_box[n].y;
        const float ux = vkx - alpha * pgx;
        const float uy = vky - alpha * pgy;
        // Nesterov momentum uses the UNCLAMPED u_{k+1} (markv1 step order): v = u + coeff*(u - u_k)
        const float ukx = u_in[n].x, uky = u_in[n].y;
        const float vx = ux + coeff * (ux - ukx);
        const float vy = uy + coeff * (uy - uky);
        // die clamp both, independently, into [0, die - size] (lower-left anchor convention)
        const float mx = die_xmax - node_box[n].w;
        const float my = die_ymax - node_box[n].h;
        coord_t uo; uo.x = clampf(ux, 0.0f, mx); uo.y = clampf(uy, 0.0f, my);
        coord_t vo; vo.x = clampf(vx, 0.0f, mx); vo.y = clampf(vy, 0.0f, my);
        u_out[n] = uo;          // commit u_{k+1} to the owned Nesterov state
        v_out.write(vo);        // stream v_{k+1} to the Memory Writer
    }
}

} // namespace plalgo

#endif // PL_ALGO_ITERATION_UPDATE_HPP
