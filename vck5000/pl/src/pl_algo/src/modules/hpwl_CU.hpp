#ifndef PL_ALGO_HPWL_CU_HPP
#define PL_ALGO_HPWL_CU_HPP

// HPWL compute unit (PL).
//
// Computes the weighted-average HPWL gradient (dW/dx, dW/dy) for every movable
// node, entirely on the PL -- no AIE. Per net: gather pin coords from DDR, reduce
// (bbox + B/C sums), then per pin apply the WA-partial formula and
// scatter-accumulate onto the node gradient. Mirrors markv1 computeHpwlPartials_CPU,
// with exp() replaced by a host-supplied LUT (exp(-d/gamma), linear-interpolated).
//
// NOTE (accuracy watch): the LUT is an approximation of exp. Expected to be
// harmless (small per-iteration errors don't accumulate across the solve), but to
// be verified against the algorithm's convergence once the loop is on the PL.
//
// All net sizes handled: pins of a net are gathered into an on-chip buffer of up
// to CAP pins; nets with degree > CAP take a (rare) re-gather fallback [added in a
// later sub-milestone]. gamma arrives as host-set scalars (inv_gamma for the
// formula, inv_lut_step for the LUT index); the normalized LUT table is uploaded
// once by the host (only the scalars change as the schedule updates gamma).

#include "host_interface.hpp"

namespace plalgo {

constexpr int HPWL_CU_CAP     = 4096;  // max pins per net held on-chip
constexpr int HPWL_CU_LUT_MAX = 1024;  // max LUT entries cached on-chip

// exp(-d/gamma) via the cached LUT (d >= 0). Beyond the table -> ~0 (underflow).
static inline float hpwl_lut_exp(const float lut[HPWL_CU_LUT_MAX], int lut_size,
                                 float inv_lut_step, float d) {
    float idx_f = d * inv_lut_step;
    int   idx   = (int)idx_f;
    if (idx >= lut_size - 1) return 0.0f;
    float frac = idx_f - (float)idx;
    return lut[idx] * (1.0f - frac) + lut[idx + 1] * frac;
}

static void hpwl_CU(const coord_t*   node_pos,   // [num_nodes] AoS {x,y}
                    const int*       net_ptr,    // [num_nets+1] CSR
                    const PinRecord* pins,       // [num_pins] net-major
                    const float*     exp_lut,    // [lut_size] exp(-t) normalized table
                    coord_t*         node_grad,  // [num_movable] accumulated (output)
                    float            inv_gamma,
                    float            inv_lut_step,
                    int              lut_size,
                    int              num_nets,
                    int              num_movable) {
    // Cache the LUT on-chip (BRAM or URAM). This avoid going to DDR for every access.
    float lut[HPWL_CU_LUT_MAX];
cache_lut:
    for (int i = 0; i < lut_size; i++) {
#pragma HLS PIPELINE II=1 // II = Initiation Interval (i.e. how many clock cycles between initiating the pipeline with new data)
        lut[i] = exp_lut[i];
    }

    // Clear the gradient buffer (fresh accumulation each eval).
clear_grad:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        coord_t z; z.x = 0.0f; z.y = 0.0f;
        node_grad[n] = z;
    }

    // Per-net on-chip pin buffer.
    float bx[HPWL_CU_CAP];
    float by[HPWL_CU_CAP];
    int   bidx[HPWL_CU_CAP];

net_loop:
    for (int net = 0; net < num_nets; net++) {
        const int beg = net_ptr[net];
        const int end = net_ptr[net + 1];
        const int deg = end - beg;
        if (deg <= 1) continue;          // degree-1 nets have no gradient
        if (deg > HPWL_CU_CAP) continue; // large-net fallback added later

        // ---- pass 1: gather pins + bounding box ----
        float maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
    gather:
        for (int p = 0; p < deg; p++) {
#pragma HLS PIPELINE II=1
            const PinRecord r = pins[beg + p];
            const coord_t   c = node_pos[r.node_idx];
            const float x = c.x + r.off_x;
            const float y = c.y + r.off_y;
            bx[p] = x; by[p] = y; bidx[p] = r.node_idx;
            if (x > maxx) maxx = x;
            if (x < minx) minx = x;
            if (y > maxy) maxy = y;
            if (y < miny) miny = y;
        }

        // ---- pass 2: reduce B/C over the net ----
        float Bpx = 0, Bmx = 0, Cpx = 0, Cmx = 0;
        float Bpy = 0, Bmy = 0, Cpy = 0, Cmy = 0;
    reduce:
        for (int p = 0; p < deg; p++) {
#pragma HLS PIPELINE II=1
            const float x = bx[p], y = by[p];
            const float apx = hpwl_lut_exp(lut, lut_size, inv_lut_step, maxx - x);
            const float amx = hpwl_lut_exp(lut, lut_size, inv_lut_step, x - minx);
            const float apy = hpwl_lut_exp(lut, lut_size, inv_lut_step, maxy - y);
            const float amy = hpwl_lut_exp(lut, lut_size, inv_lut_step, y - miny);
            Bpx += apx; Bmx += amx; Cpx += apx * x; Cmx += amx * x;
            Bpy += apy; Bmy += amy; Cpy += apy * y; Cmy += amy * y;
        }
        const float bpx2 = 1.0f / (Bpx * Bpx);
        const float bmx2 = 1.0f / (Bmx * Bmx);
        const float bpy2 = 1.0f / (Bpy * Bpy);
        const float bmy2 = 1.0f / (Bmy * Bmy);

        // ---- pass 3: per-pin partial + scatter-accumulate (movable only) ----
    partials:
        for (int p = 0; p < deg; p++) {
#pragma HLS PIPELINE II=1
            const int idx = bidx[p];
            if (idx >= num_movable) continue; // fixed node: counted in B/C, no grad
            const float x = bx[p], y = by[p];
            const float apx = hpwl_lut_exp(lut, lut_size, inv_lut_step, maxx - x);
            const float amx = hpwl_lut_exp(lut, lut_size, inv_lut_step, x - minx);
            const float apy = hpwl_lut_exp(lut, lut_size, inv_lut_step, maxy - y);
            const float amy = hpwl_lut_exp(lut, lut_size, inv_lut_step, y - miny);

            const float px = ((1.0f + x * inv_gamma) * Bpx - Cpx * inv_gamma) * (apx * bpx2)
                           - ((1.0f - x * inv_gamma) * Bmx + Cmx * inv_gamma) * (amx * bmx2);
            const float py = ((1.0f + y * inv_gamma) * Bpy - Cpy * inv_gamma) * (apy * bpy2)
                           - ((1.0f - y * inv_gamma) * Bmy + Cmy * inv_gamma) * (amy * bmy2);

            coord_t g = node_grad[idx];
            g.x += px; g.y += py;
            node_grad[idx] = g;
        }
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_CU_HPP
