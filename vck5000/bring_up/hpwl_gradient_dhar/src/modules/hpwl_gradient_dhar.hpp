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
//  * We keep sw_only's bounding-box max-shift on the exponents (E(max_x-x), E(x-min_x), ...). Dhar's
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

// Dhar's cap (Dhar et al., FPL 2019, Sec. III) -- NOT IGNORE_NET_DEGREE (100, XPlace's mask, applied
// host-side). Nets with more than this many pins are ignored here (their gradient AND their HPWL
// contribution are dropped). Dhar reports such nets are <0.25% of a real netlist and that
// ignoring them slightly IMPROVES average quality, since HPWL grossly underestimates the routed
// wirelength of large nets. Padding target for the adder tree is this value. Meow.
constexpr int MAX_NET_DEGREE = 16;

// Depth-4 balanced adder tree over 16 inputs -- Dhar's Fig. 6 in the single-net form. Fully
// unrolled: 8+4+2+1 = 15 two-input adders, any input-to-output path crosses exactly 4 (depth 4).
// Balanced (not a linear chain) is the whole point -- it is what lets the per-net loop pipeline,
// since there is no net-to-net carried dependence and the in-net chain is only 4 adds deep. Meow.
static inline float dhar_adder_tree16(const float v[MAX_NET_DEGREE]) {
#pragma HLS INLINE
    float l3[8], l2[4], l1[2];
#pragma HLS ARRAY_PARTITION variable=l3 complete dim=1
#pragma HLS ARRAY_PARTITION variable=l2 complete dim=1
#pragma HLS ARRAY_PARTITION variable=l1 complete dim=1
    for (int i = 0; i < 8; i++) { l3[i] = v[2 * i] + v[2 * i + 1]; }  // 16 terms -> 8
    for (int i = 0; i < 4; i++) { l2[i] = l3[2 * i] + l3[2 * i + 1]; } // 8 terms -> 4
    for (int i = 0; i < 2; i++) { l1[i] = l2[2 * i] + l2[2 * i + 1]; } // 4 terms -> 2
    return l1[0] + l1[1];                                              // 2 terms -> 1 result
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
    // TRIED AND REVERTED 2026-09-11 (tasks.md #40): `#pragma HLS ARRAY_PARTITION
    // variable=lut_BRAM type=cyclic factor=16 dim=1` here. It did what it was supposed to --
    // C-synthesis confirmed BRAM_18K 14->0, the 64 concurrent reads moved to 16 LUTRAM banks --
    // but a full P&R run then FAILED to route at all (1126 signals uncompleted, 771 illegal node
    // overlaps), worse than the pre-fix "routes but misses timing" baseline. LUTRAM lives in the
    // same SLICEM fabric as the surrounding fmul/fmadd/faddfsub DSP-adjacent logic in net_loop,
    // so the +42% LUT cost (69k->98k) of 16 banks landed as MORE local density in the same
    // already-congested region, not less. Net negative: don't reapply without also cutting
    // net_loop's parallelism (see the term_gen/combine unroll-width note below), which is the
    // actual size problem this was trying to route around. Meow.
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
    // HPWL_LANES named scalar accumulators, NOT an array -- this is hpwl_gradient.hpp's
    // hpwl_reduce trick (distance-1 carried dependence on a double add is too slow for II=1;
    // spreading across LANES independent registers hides the add latency), but reached via a
    // switch/case on n&(LANES-1) instead of an unrolled outer-stride loop, since net_loop already
    // processes one net per iteration and duplicating its per-net compute LANES-wide would
    // multiply the whole term-gen/adder-tree/combine block's resource cost. A dynamic array index
    // (hpwl_part[n & 7]) would still defeat it -- HLS would see one shared memory recurrence
    // regardless -- these must stay 8 distinct named variables. Confirmed load-bearing 2026-09-11
    // (tasks.md #40): v++'s own guidance report flagged this exact carried-dependence violation on
    // 'hpwl_total' at II=1..4, ahead of (and independent from) the net_pins_DDR port conflict. Meow.
    static_assert(HPWL_LANES == 8, "hpwl_total lane switch below is hand-unrolled for 8 lanes");
    double hpwl_part0 = 0.0, hpwl_part1 = 0.0, hpwl_part2 = 0.0, hpwl_part3 = 0.0;
    double hpwl_part4 = 0.0, hpwl_part5 = 0.0, hpwl_part6 = 0.0, hpwl_part7 = 0.0;
net_loop:
    for (int n = 0; n < num_nets; n++) {
#pragma HLS PIPELINE
        const int beg = net_ptr_DDR[n];
        const int end = net_ptr_DDR[n + 1];
        const int deg = end - beg;
        if (deg < 2) continue;                                   // empty / degree-1 (also net<0)
        if (net_pins_DDR[beg].net < 0) continue;                 // masked net -> no gradient
        if (deg > MAX_NET_DEGREE) continue;                 // Dhar cap: ignore, slots stay 0

        // ---- load the net into a padded-16 register block (Fig. 5 term-gen inputs) ----
        float x_block[MAX_NET_DEGREE], y_block[MAX_NET_DEGREE];
        int   npin_slot[MAX_NET_DEGREE];
#pragma HLS ARRAY_PARTITION variable=x_block complete dim=1
#pragma HLS ARRAY_PARTITION variable=y_block complete dim=1
#pragma HLS ARRAY_PARTITION variable=npin_slot complete dim=1
        float max_x = -1e30f, min_x = 1e30f, max_y = -1e30f, min_y = 1e30f;
    // PIPELINE, not UNROLL: net_pins_DDR (gmem1) and pin_to_npin_DDR (gmem3) are off-chip m_axi
    // ports, not on-chip arrays -- ARRAY_PARTITION doesn't apply to them, and UNROLL asked for
    // all 16 reads of each at once, which a single AXI port cannot serve. v++'s own HLS log
    // (both before and after the lut_BRAM fix, tasks.md #40) showed this exact conflict:
    // "Unable to schedule bus request ... on port 'gmem1' ... due to limited memory ports",
    // net_loop settling for Final II=16, Depth=411 to route around it -- unchanged by partitioning
    // lut_BRAM, which was never the binding constraint. PIPELINE turns the 16 reads into one
    // sequential burst (k increments, address is contiguous) writing into the SAME
    // ARRAY_PARTITION-complete x_block/y_block/npin_slot registers one at a time -- term_gen onward still reads
    // them fully in parallel once loaded, since nothing downstream touches DDR. Meow.
    load_net:
        for (int k = 0; k < MAX_NET_DEGREE; k++) {
#pragma HLS PIPELINE II=1
            if (k < deg) {
                const NodePin pin = net_pins_DDR[beg + k];         // net-major, contiguous -> burst
                x_block[k]   = pin.x;                                   // absolute position (P2)
                y_block[k]   = pin.y;
                npin_slot[k] = pin_to_npin_DDR[beg + k];              // node-major target, -1 if none
                if (pin.x > max_x) max_x = pin.x;
                if (pin.x < min_x) min_x = pin.x;
                if (pin.y > max_y) max_y = pin.y;
                if (pin.y < min_y) min_y = pin.y;
            } else {
                x_block[k] = 0.0f; y_block[k] = 0.0f; npin_slot[k] = -1;        // pad: neutral, never scattered
            }
        }

        // ---- term generators (Fig. 5): four terms per pin, in the bbox-shifted form ----
        // Padding slots (k>=deg) contribute 0 to every sum because their term arrays are zeroed.
        // Bpx_terms = B+ terms for x, Cmy_terms = C- terms for y, etc.
        float Bpx_terms[MAX_NET_DEGREE], Cpx_terms[MAX_NET_DEGREE];
        float Bmx_terms[MAX_NET_DEGREE], Cmx_terms[MAX_NET_DEGREE];
        float Bpy_terms[MAX_NET_DEGREE], Cpy_terms[MAX_NET_DEGREE];
        float Bmy_terms[MAX_NET_DEGREE], Cmy_terms[MAX_NET_DEGREE];
#pragma HLS ARRAY_PARTITION variable=Bpx_terms complete dim=1
#pragma HLS ARRAY_PARTITION variable=Cpx_terms complete dim=1
#pragma HLS ARRAY_PARTITION variable=Bmx_terms complete dim=1
#pragma HLS ARRAY_PARTITION variable=Cmx_terms complete dim=1
#pragma HLS ARRAY_PARTITION variable=Bpy_terms complete dim=1
#pragma HLS ARRAY_PARTITION variable=Cpy_terms complete dim=1
#pragma HLS ARRAY_PARTITION variable=Bmy_terms complete dim=1
#pragma HLS ARRAY_PARTITION variable=Cmy_terms complete dim=1
    // CONFIRMED TIMING-CLOSURE FAILURE POINT (real hardware, 2026-09-10, tasks.md #40): this
    // UNROLLed loop calls hpwl_lut_exp 4x/lane x 16 lanes = 64 concurrent lut_BRAM reads/cycle
    // (128 counting the 2-tap interpolation each call does), against a memory with 1-2 physical
    // read ports. The routed design could not close the resulting long-distance routing at
    // 300 MHz: WNS -0.710ns, 43,859/196,028 endpoints failing, ALL ten worst paths ending in
    // lut_BRAM_load_*_reg / ce_reg_replica_* inside this net_loop pipeline stage.
    // NOT a lut_BRAM-specific fix, per 2026-09-11: cyclic-partitioning lut_BRAM alone made a
    // FULL P&R run fail to route entirely (1126 unrouted signals, 771 illegal overlaps -- see the
    // revert note at lut_BRAM's declaration above). The top-10 congested nodes in that run were
    // dominated by fmul/fmadd/faddfsub DSP-adjacent logic, not lut_BRAM -- i.e. the real problem
    // is this whole term_gen+adder-tree+combine block (dozens of parallel float multiply/FMA/add
    // units, ALL 16 lanes, ALL live at once) being too DENSE for the local fabric to route, and
    // relocating one piece of it (the LUT reads) just moved the congestion, it didn't shrink it.
    // The next real lever is reducing net_loop's parallelism WIDTH -- partially unroll term_gen/
    // the adder trees/combine (e.g. 4 or 8 lanes per wave instead of 16) so less floating-point
    // hardware needs to coexist in one place, at the cost of more cycles/net. Not yet attempted.
    // Not a functional bug either way -- tier-1 (make test) passes bit-for-bit throughout; see
    // tasks.md #40 / _NEW_HANDOFF_40_hpwl_gradient_dhar_hw_grad_bug for the full trace. Meow.
    term_gen:
        for (int k = 0; k < MAX_NET_DEGREE; k++) {
#pragma HLS UNROLL
            if (k < deg) {
                // apx = a^+_x = exp(max_x - x) -- shift avoids overflow
                const float apx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, max_x - x_block[k]);
                const float amx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, x_block[k] - min_x);
                const float apy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, max_y - y_block[k]);
                const float amy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, y_block[k] - min_y);
                // amy = a^-_y = exp(y - min_y)

                Bpx_terms[k] = apx;        Cpx_terms[k] = apx * x_block[k];
                Bmx_terms[k] = amx;        Cmx_terms[k] = amx * x_block[k];
                Bpy_terms[k] = apy;        Cpy_terms[k] = apy * y_block[k];
                Bmy_terms[k] = amy;        Cmy_terms[k] = amy * y_block[k];
            } else {
                Bpx_terms[k] = Cpx_terms[k] = Bmx_terms[k] = Cmx_terms[k] = 0.0f;
                Bpy_terms[k] = Cpy_terms[k] = Bmy_terms[k] = Cmy_terms[k] = 0.0f;
            }
        }

        // ---- adder trees (Fig. 6): the four B and four C sums, depth-4 balanced ----
        const float Bpx = dhar_adder_tree16(Bpx_terms), Cpx = dhar_adder_tree16(Cpx_terms);
        const float Bmx = dhar_adder_tree16(Bmx_terms), Cmx = dhar_adder_tree16(Cmx_terms);
        const float Bpy = dhar_adder_tree16(Bpy_terms), Cpy = dhar_adder_tree16(Cpy_terms);
        const float Bmy = dhar_adder_tree16(Bmy_terms), Cmy = dhar_adder_tree16(Cmy_terms);

        const float inv_Bpx2 = 1.0f / (Bpx * Bpx), inv_Bmx2 = 1.0f / (Bmx * Bmx);
        const float inv_Bpy2 = 1.0f / (Bpy * Bpy), inv_Bmy2 = 1.0f / (Bmy * Bmy);

        // ---- combiners (Fig. 8, eq. 4) + scatter to node-major order ----
    combine:
        for (int k = 0; k < MAX_NET_DEGREE; k++) {
#pragma HLS UNROLL
            if (k >= deg || npin_slot[k] < 0) continue;               // pad, or fixed-node pin: no slot
            const float pin_x = x_block[k], pin_y = y_block[k];
            // Bpx_terms holds apx values
            const float partial_x = ((1.0f + pin_x * inv_gamma) * Bpx - Cpx * inv_gamma) * (Bpx_terms[k] * inv_Bpx2)
                           - ((1.0f - pin_x * inv_gamma) * Bmx + Cmx * inv_gamma) * (Bmx_terms[k] * inv_Bmx2);
            const float partial_y = ((1.0f + pin_y * inv_gamma) * Bpy - Cpy * inv_gamma) * (Bpy_terms[k] * inv_Bpy2)
                           - ((1.0f - pin_y * inv_gamma) * Bmy + Cmy * inv_gamma) * (Bmy_terms[k] * inv_Bmy2);
            coord_t pin_grad; pin_grad.x = partial_x; pin_grad.y = partial_y;
            // store gradients in node-major order (phase 3 will sum them per node)
            pin_grad_DDR[npin_slot[k]] = pin_grad; // injective scatter (no two inputs yield the same output) -> II=1
        }

        // HPWL by-product: half-perimeter of the net's bbox (same bbox as the gradient).
        // Static-index lane pick via switch/case -- see the LANES comment above net_loop.
        const double net_hpwl = (double)((max_x - min_x) + (max_y - min_y));
        switch (n & 7) {
            case 0: hpwl_part0 += net_hpwl; break;
            case 1: hpwl_part1 += net_hpwl; break;
            case 2: hpwl_part2 += net_hpwl; break;
            case 3: hpwl_part3 += net_hpwl; break;
            case 4: hpwl_part4 += net_hpwl; break;
            case 5: hpwl_part5 += net_hpwl; break;
            case 6: hpwl_part6 += net_hpwl; break;
            default: hpwl_part7 += net_hpwl; break;
        }
    }
    const double hpwl_total = hpwl_part0 + hpwl_part1 + hpwl_part2 + hpwl_part3
                             + hpwl_part4 + hpwl_part5 + hpwl_part6 + hpwl_part7;
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
    float node_grad_x = 0.0f, node_grad_y = 0.0f;
node_reduce: // Segmented Reduction over node-major pin_grad_DDR. Sum each node's pin gradients
    for (int p = 0; p < num_node_pins; p++) {
#pragma HLS PIPELINE
        const int     node     = node_pins_DDR[p].node_idx;   // sequential (segment key)
        const coord_t pin_grad = pin_grad_DDR[p];             // sequential (node-major layout)
        if (node != cur_node) {                               // node boundary -> flush previous
            if (cur_node >= 0) {
                coord_t node_grad; node_grad.x = node_grad_x; node_grad.y = node_grad_y;
                node_grad_DDR[cur_node] = node_grad;
            }
            cur_node = node; node_grad_x = 0.0f; node_grad_y = 0.0f;
        }
        node_grad_x += pin_grad.x; node_grad_y += pin_grad.y;
    }
    if (cur_node >= 0) {                                       // flush last node
        coord_t node_grad; node_grad.x = node_grad_x; node_grad.y = node_grad_y;
        node_grad_DDR[cur_node] = node_grad;
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_GRADIENT_DHAR_HPP
