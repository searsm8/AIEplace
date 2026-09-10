#ifndef PL_ALGO_HPWL_GRADIENT_DHAR_HPP
#define PL_ALGO_HPWL_GRADIENT_DHAR_HPP

// HPWL gradient compute unit (PL) -- Dhar's fast-adder-tree method, max net degree 16.
//
// Same weighted-average HPWL gradient (dW/dx, dW/dy) as hpwl_gradient.hpp, verified against the
// same sw_only golden (computeHpwlPartials_CPU, Partials.cpp), but built the way Dhar et al.
// accelerate it on FPGA ("FPGA Accelerated FPGA Placement", FPL 2019, extracted under
// .claude/2_ARTIFACTS/papers/dhar_fpga_accel/). The distinctive move is Dhar's Method-1 datapath:
// term generators (Fig. 5) -> a fast, balanced adder tree (Fig. 6) -> combiners implementing the
// WA partial, eq. 4 (Fig. 8) -- with a HARD CAP of 16 pins per net.
//
// WHY A PER-NET DATAPATH, NOT THREE STREAMING PASSES. The sibling hpwl_gradient.hpp handles
// arbitrary net degree, so it cannot hold a whole net on-chip: it streams pins three times and
// spills per-net scalars (bb_DDR/sums_DDR) to DDR. The 16-pin cap removes that constraint -- an
// entire net (<=16 pins) fits in registers -- so bbox, the four B/C sums, and every pin's
// gradient are computed in ONE per-net pass with NO DDR spill. bb_DDR/sums_DDR disappear from the
// signature entirely; that is a direct, intended consequence of the cap, not an omission.
//
//   PHASE Z  -- zero pin_grad_DDR[0..num_node_pins). Load-bearing: nets with degree > 16 (Dhar
//               ignores them) are never scattered, so their node-major slots must already read 0
//               for the phase-3 reduction to be correct. One sequential burst.
//   PHASE D  -- per net (net-major CSR): skip masked (net<0) and oversized (deg>16) nets; for a
//               live 2..16-pin net load its pins into a padded-16 register block, reduce to a
//               bbox, generate the four terms per pin, sum each with a depth-4 adder tree, then
//               run 16 combiners and SCATTER each live pin's 2-float gradient to node-major order
//               via pin_to_npin_DDR. HPWL (half-perimeter of the same bbox) is accumulated here.
//   PHASE 3  -- per-node segmented reduction over the node-major pin_grad_DDR. Identical to
//               hpwl_gradient.hpp phase 3: pin_grad_DDR is already node-major, so it is a pure
//               sequential stream summing each node's pin gradients.
//
// DELIBERATE DIVERGENCE FROM DHAR (documented, so a later session does not "fix" it back):
//  * We keep sw_only's bounding-box max-shift on the exponents (E(mxx-x), E(x-mnx), ...). Dhar's
//    eq. 7 feeds raw x^ = gamma*x and treats the max-tree normalization as OPTIONAL ("very little
//    impact on solution quality"). Here the shift is REQUIRED: it is what makes this module match
//    the CPU golden bit-closely, and it keeps the exponentials in [0,1] (no overflow). Same math
//    as hpwl_gradient.hpp phase 2/2.5.
//  * Dhar packs several same-degree nets into one 16-slot block and uses a multi-output adder tree
//    (34 contiguous-segment outputs, Fig. 6) plus per-index result selectors (Fig. 7) to reach
//    ~90% slot utilization. That block-packing is a UTILIZATION optimization on top of the same
//    arithmetic; this draft processes one net per pass (padding to 16). It is the functional block
//    to verify against the golden first; the multi-net packing is the follow-on hardware-opt step.
//
// exp() is the same host-supplied LUT (exp(-d/gamma), linear-interpolated) as the sibling module;
// hpwl_lut_exp and the refresh passes are reused from hpwl_gradient.hpp (included below).

#include "modules/hpwl_gradient.hpp"   // reuse hpwl_lut_exp + refresh_net_pins/refresh_node_pins

namespace plalgo {

// Dhar's cap: nets with more than this many pins are ignored (their gradient AND their HPWL
// contribution are dropped). Dhar reports such nets are <0.25% of a real netlist and that
// ignoring them slightly IMPROVES average quality, since HPWL grossly underestimates the routed
// wirelength of large nets. Padding target for the adder tree is this value. Meow.
constexpr int DHAR_MAX_NET_DEGREE = 16;

// Depth-4 balanced adder tree over 16 inputs -- Dhar's Fig. 6 in the single-net form. Fully
// unrolled: 8+4+2+1 = 15 two-input adders, any input-to-output path crosses exactly 4 (depth 4).
// Balanced (not a linear chain) is the whole point -- it is what lets the per-net loop pipeline,
// since there is no net-to-net carried dependence and the in-net chain is only 4 adds deep. Meow.
static inline float dhar_adder_tree16(const float v[DHAR_MAX_NET_DEGREE]) {
#pragma HLS INLINE
    float l3[8], l2[4], l1[2];
#pragma HLS ARRAY_PARTITION variable=l3 complete dim=1
#pragma HLS ARRAY_PARTITION variable=l2 complete dim=1
#pragma HLS ARRAY_PARTITION variable=l1 complete dim=1
    for (int i = 0; i < 8; i++) { l3[i] = v[2 * i] + v[2 * i + 1]; }  // 16 -> 8
    for (int i = 0; i < 4; i++) { l2[i] = l3[2 * i] + l3[2 * i + 1]; } // 8 -> 4
    for (int i = 0; i < 2; i++) { l1[i] = l2[2 * i] + l2[2 * i + 1]; } // 4 -> 2
    return l1[0] + l1[1];                                              // 2 -> 1
}

// HPWL gradient via Dhar's method. Same I/O contract as hpwl_gradient (net-major pins carrying
// absolute positions; node-major pins + a static net-major->node-major permutation for the
// scatter), MINUS bb_DDR/sums_DDR: the fused per-net pass keeps those on-chip.
//
// MUST run after refresh_net_pins/refresh_node_pins at the current probe (see hpwl_gradient.hpp).
static void hpwl_gradient_dhar(
        const int*     net_ptr_DDR,     // [num_nets+1] CSR offsets into net_pins
        const NodePin* net_pins_DDR,    // [num_pins]  NET-major, absolute positions (phase D)
        const NodePin* node_pins_DDR,   // [num_node_pins] NODE-major (phase 3: node_idx only)
        const int*     pin_to_npin_DDR, // [num_pins] net-major -> node-major slot, -1 if none
        const float*   exp_lut_DDR,     // [lut_size] exp(-t) table
        coord_t*       pin_grad_DDR,    // [num_node_pins] scratch (D scatters, 3 reduces)
        coord_t*       node_grad_DDR,   // [num_movable] gradient (output)
        float*         out_hpwl_DDR,    // [1] total HPWL over <=16-pin nets (output)
        float          inv_gamma,
        float          inv_lut_step,
        int            lut_size,
        int            num_nets,
        int            num_movable,
        int            num_node_pins) {

    // Cache the LUT on-chip (avoids a DDR access per exp lookup) -- same as hpwl_gradient.
    float lut_BRAM[HPWL_GRADIENT_LUT_MAX];
cache_lut:
    for (int i = 0; i < lut_size; i++) {
#pragma HLS PIPELINE II=1
        lut_BRAM[i] = exp_lut_DDR[i];
    }

    // ===== PHASE Z: zero the node-major pin gradient scratch =====
    // Oversized (deg>16) and never-written slots must read 0 so phase 3 sums them harmlessly.
    // Sequential -> burst. This replaces the sibling module's "every live pin is written exactly
    // once" invariant, which the 16-cap breaks (a >16 pin is live to the host but dropped here).
zero_pin_grad:
    for (int p = 0; p < num_node_pins; p++) {
#pragma HLS PIPELINE II=1
        coord_t z; z.x = 0.0f; z.y = 0.0f;
        pin_grad_DDR[p] = z;
    }

    // ===== PHASE D: per-net term-gen -> adder tree -> combiners -> scatter =====
    double hpwl_total = 0.0;
net_loop:
    for (int n = 0; n < num_nets; n++) {
#pragma HLS PIPELINE
        const int beg = net_ptr_DDR[n];
        const int end = net_ptr_DDR[n + 1];
        const int deg = end - beg;
        if (deg < 2) continue;                                   // empty / degree-1 (also net<0)
        if (net_pins_DDR[beg].net < 0) continue;                 // masked net -> no gradient
        if (deg > DHAR_MAX_NET_DEGREE) continue;                 // Dhar cap: ignore, slots stay 0

        // ---- load the net into a padded-16 register block (Fig. 5 term-gen inputs) ----
        float xv[DHAR_MAX_NET_DEGREE], yv[DHAR_MAX_NET_DEGREE];
        int   slot[DHAR_MAX_NET_DEGREE];
#pragma HLS ARRAY_PARTITION variable=xv complete dim=1
#pragma HLS ARRAY_PARTITION variable=yv complete dim=1
#pragma HLS ARRAY_PARTITION variable=slot complete dim=1
        float mxx = -1e30f, mnx = 1e30f, mxy = -1e30f, mny = 1e30f;
    load_net:
        for (int k = 0; k < DHAR_MAX_NET_DEGREE; k++) {
#pragma HLS UNROLL
            if (k < deg) {
                const NodePin r = net_pins_DDR[beg + k];         // net-major, contiguous -> burst
                xv[k]   = r.x;                                   // absolute position (P2)
                yv[k]   = r.y;
                slot[k] = pin_to_npin_DDR[beg + k];              // node-major target, -1 if none
                if (r.x > mxx) mxx = r.x;
                if (r.x < mnx) mnx = r.x;
                if (r.y > mxy) mxy = r.y;
                if (r.y < mny) mny = r.y;
            } else {
                xv[k] = 0.0f; yv[k] = 0.0f; slot[k] = -1;        // pad: neutral, never scattered
            }
        }

        // ---- term generators (Fig. 5): four terms per pin, in the bbox-shifted form ----
        // Padding slots (k>=deg) contribute 0 to every sum because their term arrays are zeroed.
        float tBpx[DHAR_MAX_NET_DEGREE], tCpx[DHAR_MAX_NET_DEGREE];
        float tBmx[DHAR_MAX_NET_DEGREE], tCmx[DHAR_MAX_NET_DEGREE];
        float tBpy[DHAR_MAX_NET_DEGREE], tCpy[DHAR_MAX_NET_DEGREE];
        float tBmy[DHAR_MAX_NET_DEGREE], tCmy[DHAR_MAX_NET_DEGREE];
#pragma HLS ARRAY_PARTITION variable=tBpx complete dim=1
#pragma HLS ARRAY_PARTITION variable=tCpx complete dim=1
#pragma HLS ARRAY_PARTITION variable=tBmx complete dim=1
#pragma HLS ARRAY_PARTITION variable=tCmx complete dim=1
#pragma HLS ARRAY_PARTITION variable=tBpy complete dim=1
#pragma HLS ARRAY_PARTITION variable=tCpy complete dim=1
#pragma HLS ARRAY_PARTITION variable=tBmy complete dim=1
#pragma HLS ARRAY_PARTITION variable=tCmy complete dim=1
        float apx_s[DHAR_MAX_NET_DEGREE], amx_s[DHAR_MAX_NET_DEGREE];
        float apy_s[DHAR_MAX_NET_DEGREE], amy_s[DHAR_MAX_NET_DEGREE];
#pragma HLS ARRAY_PARTITION variable=apx_s complete dim=1
#pragma HLS ARRAY_PARTITION variable=amx_s complete dim=1
#pragma HLS ARRAY_PARTITION variable=apy_s complete dim=1
#pragma HLS ARRAY_PARTITION variable=amy_s complete dim=1
    term_gen:
        for (int k = 0; k < DHAR_MAX_NET_DEGREE; k++) {
#pragma HLS UNROLL
            if (k < deg) {
                const float apx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, mxx - xv[k]);
                const float amx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, xv[k] - mnx);
                const float apy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, mxy - yv[k]);
                const float amy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, yv[k] - mny);
                apx_s[k] = apx; amx_s[k] = amx; apy_s[k] = apy; amy_s[k] = amy;
                tBpx[k] = apx;        tCpx[k] = apx * xv[k];
                tBmx[k] = amx;        tCmx[k] = amx * xv[k];
                tBpy[k] = apy;        tCpy[k] = apy * yv[k];
                tBmy[k] = amy;        tCmy[k] = amy * yv[k];
            } else {
                apx_s[k] = amx_s[k] = apy_s[k] = amy_s[k] = 0.0f;
                tBpx[k] = tCpx[k] = tBmx[k] = tCmx[k] = 0.0f;
                tBpy[k] = tCpy[k] = tBmy[k] = tCmy[k] = 0.0f;
            }
        }

        // ---- adder trees (Fig. 6): the four B and four C sums, depth-4 balanced ----
        const float Bpx = dhar_adder_tree16(tBpx), Cpx = dhar_adder_tree16(tCpx);
        const float Bmx = dhar_adder_tree16(tBmx), Cmx = dhar_adder_tree16(tCmx);
        const float Bpy = dhar_adder_tree16(tBpy), Cpy = dhar_adder_tree16(tCpy);
        const float Bmy = dhar_adder_tree16(tBmy), Cmy = dhar_adder_tree16(tCmy);

        const float ipx = 1.0f / (Bpx * Bpx), imx = 1.0f / (Bmx * Bmx);
        const float ipy = 1.0f / (Bpy * Bpy), imy = 1.0f / (Bmy * Bmy);

        // ---- combiners (Fig. 8, eq. 4) + scatter to node-major order ----
    combine:
        for (int k = 0; k < DHAR_MAX_NET_DEGREE; k++) {
#pragma HLS UNROLL
            if (k >= deg || slot[k] < 0) continue;               // pad, or fixed-node pin: no slot
            const float x = xv[k], y = yv[k];
            const float px = ((1.0f + x * inv_gamma) * Bpx - Cpx * inv_gamma) * (apx_s[k] * ipx)
                           - ((1.0f - x * inv_gamma) * Bmx + Cmx * inv_gamma) * (amx_s[k] * imx);
            const float py = ((1.0f + y * inv_gamma) * Bpy - Cpy * inv_gamma) * (apy_s[k] * ipy)
                           - ((1.0f - y * inv_gamma) * Bmy + Cmy * inv_gamma) * (amy_s[k] * imy);
            coord_t g; g.x = px; g.y = py;
            pin_grad_DDR[slot[k]] = g;                            // injective scatter -> II=1
        }

        // HPWL by-product: half-perimeter of the net's bbox (same bbox as the gradient).
        hpwl_total += (double)((mxx - mnx) + (mxy - mny));
    }
    *out_hpwl_DDR = (float)hpwl_total;   // narrow only at the boundary, as metrics does

    // ===== PHASE 3: per-node segmented reduction (identical to hpwl_gradient.hpp) =====
    // pin_grad_DDR is node-major (phase D scattered through the same permutation node_pins is
    // sorted by), so pin_grad_DDR[p] is the gradient of node_pins_DDR[p]. Pure sequential stream.
clear_grad:
    for (int nd = 0; nd < num_movable; nd++) {
#pragma HLS PIPELINE II=1
        coord_t z; z.x = 0.0f; z.y = 0.0f;
        node_grad_DDR[nd] = z;
    }

    int   cur_node = -1;
    float ax = 0.0f, ay = 0.0f;
node_reduce:
    for (int p = 0; p < num_node_pins; p++) {
#pragma HLS PIPELINE
        const int     node = node_pins_DDR[p].node_idx;   // sequential (segment key)
        const coord_t g    = pin_grad_DDR[p];             // sequential (node-major layout)
        if (node != cur_node) {                           // node boundary -> flush previous
            if (cur_node >= 0) {
                coord_t o; o.x = ax; o.y = ay;
                node_grad_DDR[cur_node] = o;
            }
            cur_node = node; ax = 0.0f; ay = 0.0f;
        }
        ax += g.x; ay += g.y;
    }
    if (cur_node >= 0) {                                   // flush last node
        coord_t o; o.x = ax; o.y = ay;
        node_grad_DDR[cur_node] = o;
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_GRADIENT_DHAR_HPP
