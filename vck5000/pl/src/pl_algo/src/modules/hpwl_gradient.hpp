#ifndef PL_ALGO_HPWL_GRADIENT_HPP
#define PL_ALGO_HPWL_GRADIENT_HPP

// HPWL gradient compute unit (PL).
//
// Computes the weighted-average HPWL gradient (dW/dx, dW/dy) for every movable
// node, entirely on the PL -- no AIE. Math mirrors sw_only computeHpwlPartials_CPU,
// with exp() replaced by a host-supplied LUT (exp(-d/gamma), linear-interpolated).
//
// THREE SEGMENTED REDUCTIONS, FULLY ARBITRARY-SIZE. Every phase streams a flat pin
// list and reduces by SEGMENT (a contiguous run of pins sharing a key): accumulate
// in registers, flush the result the instant the key changes. No on-chip per-key
// accumulator array, so no tiling and no size cap -- the only per-key state is a
// few registers. This is the CSR/SpMV segmented-reduction pattern; phases A and B
// are a sparse adjacency and its transpose. (Memory suffix = location: _DDR off
// chip, _BRAM on chip; bare names are registers.)
//
//   PHASE A1 -- bbox, segmented over NETS (pins_DDR, net-major). Reduce each net's
//   pin coords to a bounding box in registers; write bb_DDR[net] at the net change.
//   PHASE A2 -- B/C sums, segmented over NETS (pins_DDR again). At each net change
//   read that net's final bb_DDR[net]; accumulate exp-weighted B/C sums in
//   registers; write sums_DDR[net] at the next change.
//   Two passes, not one, and it is NOT because of a degree cap -- a cap exists
//   (IGNORE_NET_DEGREE = 100, host-enforced). Fusing them behind a FIFO was
//   investigated and rejected on measurement: adaptec1's unmasked nets have MEDIAN
//   DEGREE 2 (53% are degree-2), so a per-net drain loop pays pipeline fill/drain on
//   a 166-deep datapath and lands ~4.4M cycles against phase 2's 2.8M. Re-streaming
//   net_pins is sequential and burstable; it is the cheap half. See REPORT_20 P1.
//   PHASE B  -- gradient, segmented over NODES (node_pins_DDR, node-major sorted).
//   Read bb_DDR/sums_DDR[net] per pin (random READ-ONLY -> II=1); accumulate the WA
//   partial in registers; write node_grad_DDR[node] once at the node change. The
//   output is write-once in node order (-> sequential, burst), never read-modified.
//
// bb_DDR/sums_DDR (DDR scratch, [num_nets]) bridge A->B so phase B, in node order,
// can read any net's reduction. node_grad in DDR -> arbitrary num_movable.
//
// NOTE (accuracy watch): the LUT is an approximation of exp. Expected harmless
// (small per-iteration errors don't accumulate across the solve), to be re-checked
// against convergence once the full iteration loop is on the PL.

#include "host_interface.hpp"

namespace plalgo {

constexpr int HPWL_GRADIENT_LUT_MAX = 1024;  // max LUT entries cached on-chip

// Parallel accumulator lanes for the HPWL by-product reduction: the UNROLL factor, i.e. the
// number of physical double-adders instantiated, each summing an independent slice of the nets.
// Power of two so lane selection is a mask, not a modulo. 8 keeps each lane's dependent double
// adds >= 8 nets apart, which hides the add latency -- see hpwl_reduce. Meow.
constexpr int HPWL_LANES = 8;

// compute exp(-d/gamma) via the cached LUT (d >= 0). Beyond the table -> ~0 (underflow).
// Force-inline (the sweeps call it 4x/iteration): left as a shared instance HLS
// serializes the 4 calls (14-cyc latency each); inlined they pipeline independently.
static inline float hpwl_lut_exp(const float lut_BRAM[HPWL_GRADIENT_LUT_MAX], int lut_size,
                                 float inv_lut_step, float d) {
#pragma HLS INLINE
    float idx_f = d * inv_lut_step;
    int   idx   = (int)idx_f;
    if (idx >= lut_size - 1) return 0.0f;
    float frac = idx_f - (float)idx;
    return lut_BRAM[idx] * (1.0f - frac) + lut_BRAM[idx + 1] * frac;
}

// NOTE: takes no node_pos. Pin records carry their absolute position (P2), so every loop here
// is a pure sequential stream -- the random gathers this used to do are now a single pass in
// refresh_net_pins / refresh_node_pins (below in this file), which must run first.
static void hpwl_gradient(const int*     net_ptr_DDR,    // [num_nets+1] CSR (unused: kept for ABI)
                    const NodePin* net_pins_DDR,       // [num_pins] NET-major (phase 1&2)
                    const NodePin* node_pins_DDR,      // [num_node_pins] NODE-major (phase 3)
                    const float*   exp_lut_DDR,    // [lut_size] exp(-t) table
                    NetBBox*       bb_DDR,         // [num_nets] scratch (A writes, B reads)
                    NetSums*       sums_DDR,       // [num_nets] scratch (A writes, B reads)
                    coord_t*       node_grad_DDR,  // [num_movable] gradient (output)
                    float*         out_hpwl_DDR,   // [1] total HPWL at these positions (output)
                    float          inv_gamma,
                    float          inv_lut_step,
                    int            lut_size,
                    int            num_nets,
                    int            num_movable,
                    int            num_node_pins) {
    const int num_pins = net_ptr_DDR[num_nets];   // CSR end == total pin records

    // Cache the LUT on-chip (avoids a DDR access per exp lookup).
    float lut_BRAM[HPWL_GRADIENT_LUT_MAX];
cache_lut:
    for (int i = 0; i < lut_size; i++) {
#pragma HLS PIPELINE II=1
        lut_BRAM[i] = exp_lut_DDR[i];
    }

    // ===== PHASE 1: bounding box, segmented over nets (register-accumulated) =====
    // HPWL falls out of this pass for free: the half-perimeter of the box we are already
    // flushing IS this net's HPWL contribution, so summing it here deletes a whole separate
    // net-major sweep (metrics::hpwl_sweep, which re-derives the same box with its own random
    // node_pos gather). Same nets, same mask, same positions -- see REPORT_20.
    //
    // The HPWL sum does NOT live in this loop -- see hpwl_reduce below for why.
    int   bb_net = -1;                              // current segment's net
    float maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
sweep_bbox:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE
        const NodePin r = net_pins_DDR[p];
        if (r.net < 0) continue;                   // pin of a no-gradient net
        if (r.net != bb_net) {                      // net boundary -> flush previous
            if (bb_net >= 0) {
                NetBBox b; b.mxx = maxx; b.mnx = minx; b.mxy = maxy; b.mny = miny;
                bb_DDR[bb_net] = b;
            }
            bb_net = r.net;
            maxx = -1e30f; minx = 1e30f; maxy = -1e30f; miny = 1e30f;
        }
        const float x = r.x;                        // absolute position, already folded in
        const float y = r.y;
        if (x > maxx) maxx = x;
        if (x < minx) minx = x;
        if (y > maxy) maxy = y;
        if (y < miny) miny = y;
    }
    if (bb_net >= 0) {                              // flush last net
        NetBBox b; b.mxx = maxx; b.mnx = minx; b.mxy = maxy; b.mny = miny;
        bb_DDR[bb_net] = b;
    }

    // ===== HPWL by-product: sum the half-perimeters phase 1 just wrote =====
    // Every net's HPWL contribution is the half-perimeter of the box already in bb_DDR, so this
    // replaces a whole separate net-major pin sweep (metrics::hpwl_sweep, which re-derives the
    // same boxes with its own random node_pos gather). Same nets, same mask, same positions.
    //
    // This is a SEPARATE loop over num_nets, not an accumulator inside sweep_bbox, and that is
    // the entire design point. Both were built and C-synthesized (2026-08-28):
    //   accumulate at the net boundary in sweep_bbox -> sweep_bbox II 2 -> 7 over num_pins.
    //     HLS sees a distance-1 carried dependence on the double add. Rotating over 8 partials
    //     does NOT help (with a dynamic index it treats the array as a memory and reports a
    //     store->load dependence regardless of the rotation). metrics::hpwl_sweep has the
    //     identical pathology on its own hpwl_total, which is why THAT loop is II~7 as well.
    //   reduce here, over num_nets                   -> sweep_bbox stays II 2; this loop is
    //     II 8 over num_nets/8 iterations = num_nets cycles total (221k on adaptec1).
    // That is ~30x cheaper than the pass it replaces (metrics::hpwl_sweep, ~944k pins at II 7
    // = 6.6M cycles), because num_nets < num_pins and the read is sequential and burstable.
    //
    // Unrolled HPWL_LANES-wide with STATIC accumulator indices -- static is what makes each
    // partial a register rather than a memory, so the 8 dependence chains run in parallel.
    // Measured cost of this block: LUT +7941 (+2.4% of device), FF +5321, BRAM 0, timing slack
    // unchanged. Nearly all of that is the 8 parallel double adders, so HPWL_LANES is the
    // knob if LUT gets tight -- halving it roughly halves the adder cost and doubles this
    // loop's cycles, which is noise against the 6.6M it saves.
    //
    // ⚠️ REQUIRES bb_DDR TO BE ZEROED BEFORE FIRST USE. Masked nets are never written by
    // sweep_bbox, so they keep whatever the buffer held; zeroed, they contribute a zero-extent
    // box and drop out of the sum for free. One host-side memset at allocation is enough -- the
    // net set is static, so masked entries stay zero for the whole run. See host_interface.hpp.
    double hpwl_part[HPWL_LANES];
#pragma HLS ARRAY_PARTITION variable=hpwl_part complete dim=1
hpwl_init:
    for (int i = 0; i < HPWL_LANES; i++) {
#pragma HLS UNROLL
        hpwl_part[i] = 0.0;
    }
    // The two loops together just walk every net 0..num_nets-1: the outer strides by
    // HPWL_LANES, the inner (fully unrolled) covers all HPWL_LANES of one stride, so
    // idx = n + k enumerates them all. The pragmas only do their work BECAUSE of this shape: Meow.
    //   PIPELINE (outer) issues one stride per cycle -- but a stride is HPWL_LANES adds, so
    //     the pipeline only closes if those adds don't chain, which the split below guarantees.
    //   UNROLL (inner) replicates the datapath into HPWL_LANES physical double-adders, and --
    //     the load-bearing part -- makes k a COMPILE-TIME constant, so hpwl_part[k] is a distinct
    //     register per lane, not an indexed memory. Each lane then has its own carried add whose
    //     next use is HPWL_LANES strides away, hiding the double-add latency. A dynamic index
    //     (hpwl_part[n % 8]) would defeat this -- HLS models the array as memory and reports one
    //     shared store->load recurrence regardless of the rotation. Static index is the whole trick.
hpwl_reduce:
    for (int n = 0; n < num_nets; n += HPWL_LANES) {
#pragma HLS PIPELINE
        for (int k = 0; k < HPWL_LANES; k++) {
#pragma HLS UNROLL
            const int idx = n + k;
            if (idx < num_nets) {                          // tail guard: num_nets need not divide
                                                           // HPWL_LANES; static k -> per-lane
                                                           // predication, not a data-dependent branch
                const NetBBox b = bb_DDR[idx];             // sequential in idx -> burstable
                // net's HPWL contribution = half-perimeter = x-extent + y-extent. Sum in double,
                // net order, matching metrics so the harness bit-comparison holds (narrowed at *out).
                hpwl_part[k] += (double)((b.mxx - b.mnx) + (b.mxy - b.mny));
            }
        }
    }
    double hpwl_total = 0.0;
hpwl_combine:
    for (int i = 0; i < HPWL_LANES; i++)
        hpwl_total += hpwl_part[i];
    *out_hpwl_DDR = (float)hpwl_total;              // narrow only at the boundary, as metrics does

    // ===== PHASE 2: B/C sums, segmented over nets (bb_DDR is final) =====
    int     bc_net = -1;
    NetBBox b{};                                    // current net's bbox (read at boundary)
    float Bpx = 0, Bmx = 0, Cpx = 0, Cmx = 0, Bpy = 0, Bmy = 0, Cpy = 0, Cmy = 0;
sweep_sums:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE
        const NodePin r = net_pins_DDR[p]; // Read pin data (one big data block, burstable)
        if (r.net < 0) continue;
        if (r.net != bc_net) {                      // net boundary -> flush previous
            if (bc_net >= 0) {
                NetSums s; s.Bpx = Bpx; s.Bmx = Bmx; s.Cpx = Cpx; s.Cmx = Cmx;
                          s.Bpy = Bpy; s.Bmy = Bmy; s.Cpy = Cpy; s.Cmy = Cmy;
                sums_DDR[bc_net] = s;
            }
            bc_net = r.net;
            b = bb_DDR[r.net];                       // this net's final bbox (once/net)
            Bpx = Bmx = Cpx = Cmx = Bpy = Bmy = Cpy = Cmy = 0.0f;
        }
        const float x = r.x;                        // absolute position, already folded in
        const float y = r.y;
        const float apx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, b.mxx - x);
        const float amx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, x - b.mnx);
        const float apy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, b.mxy - y);
        const float amy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, y - b.mny);
        Bpx += apx; Bmx += amx; Cpx += apx * x; Cmx += amx * x;
        Bpy += apy; Bmy += amy; Cpy += apy * y; Cmy += amy * y;
    }
    if (bc_net >= 0) {                              // flush last net
        NetSums s; s.Bpx = Bpx; s.Bmx = Bmx; s.Cpx = Cpx; s.Cmx = Cmx;
                  s.Bpy = Bpy; s.Bmy = Bmy; s.Cpy = Cpy; s.Cmy = Cmy;
        sums_DDR[bc_net] = s;
    }

    // ===== PHASE 3: per-node gradient, segmented over nodes =====
    // node_grad is write-once per node; nodes with no gradient-bearing pin never
    // appear in node_pins, so zero the output first (sequential -> burst).
clear_grad:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        coord_t z; z.x = 0.0f; z.y = 0.0f;
        node_grad_DDR[n] = z;
    }

    int   cur_node = -1;
    float ax = 0.0f, ay = 0.0f;
seg_reduce:
    for (int p = 0; p < num_node_pins; p++) {
#pragma HLS PIPELINE
        const NodePin r = node_pins_DDR[p];
        if (r.node_idx != cur_node) {               // node boundary -> flush previous
            if (cur_node >= 0) {
                coord_t g; g.x = ax; g.y = ay;
                node_grad_DDR[cur_node] = g;
            }
            cur_node = r.node_idx; ax = 0.0f; ay = 0.0f;
        }
        const float x = r.x;                        // absolute position, already folded in
        const float y = r.y;


        const NetBBox bb = bb_DDR[r.net];           // random READ-ONLY -> II=1
        const NetSums s  = sums_DDR[r.net];

        const float apx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, bb.mxx - x);
        const float amx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, x - bb.mnx);
        const float apy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, bb.mxy - y);
        const float amy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, y - bb.mny);
        const float bpx2 = 1.0f / (s.Bpx * s.Bpx);
        const float bmx2 = 1.0f / (s.Bmx * s.Bmx);
        const float bpy2 = 1.0f / (s.Bpy * s.Bpy);
        const float bmy2 = 1.0f / (s.Bmy * s.Bmy);
        const float px = ((1.0f + x * inv_gamma) * s.Bpx - s.Cpx * inv_gamma) * (apx * bpx2)
                       - ((1.0f - x * inv_gamma) * s.Bmx + s.Cmx * inv_gamma) * (amx * bmx2);
        const float py = ((1.0f + y * inv_gamma) * s.Bpy - s.Cpy * inv_gamma) * (apy * bpy2)
                       - ((1.0f - y * inv_gamma) * s.Bmy + s.Cmy * inv_gamma) * (amy * bmy2);
        ax += px; ay += py;
    }
    if (cur_node >= 0) {                             // flush last node
        coord_t g; g.x = ax; g.y = ay;
        node_grad_DDR[cur_node] = g;
    }
}

// ===================================================================================
// Pin-position refresh (P2) -- a subroutine of THIS compute unit, run once per iteration
// before the sweeps above. It folds the current probe v_k into both pin arrays so every
// sweep reads a pin's absolute position straight out of its NodePin record instead of
// gathering node_pos[node_idx]. Those gathers WERE the measured bottleneck (REPORT_20 §2.2:
// gmem0 was one of only three ports HLS could infer no burst for; §2.5 estimated the module
// ran ~4.7x above its compute floor because of them). Concentrating the gather here buys:
//   - it happens ONCE per iteration instead of three times (phases 1, 2, and metrics);
//   - the loop has no dependent float datapath, so a stalled read stalls nothing but itself,
//     and its port can be tuned (deep outstanding queues, top.cpp) purely for gather throughput;
//   - it is a self-contained producer, so it can later overlap with the density solve in a
//     DATAFLOW region -- impossible for a gather buried in an accumulator recurrence.
// These were a separate refresh_pin_pos.hpp; they are only ever the prep pass for the sweeps
// here, never called independently, so they live with the CU they feed. Meow.
//
// MUST RUN BEFORE THE SWEEPS, AT THE SAME PROBE. The arrays it writes are per-iteration state
// carrying v_k; running a sweep without refreshing first evaluates it at stale positions, which
// does not crash and does not look wrong -- it silently optimizes the previous iterate. See
// host_interface.hpp NodePin.

// NET-major refresh. node_idx is arbitrary here (pins are ordered by net), so node_pos_DDR is a
// TRUE random gather -- the one that survives P2, and the reason gmem0 carries a deep
// outstanding-request queue (top.cpp). Everything else is sequential and burstable: the record
// is read, patched and written back in place. Meow.
static void refresh_net_pins(const coord_t*   node_pos_DDR,   // [num_nodes] current probe v_k
                             const PinOffset* pin_off_DDR,    // [num_pins] static, upload-once
                             NodePin*         net_pins_DDR,   // [num_pins] patched in place
                             int              num_pins) {
refresh_net:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE II=1
        NodePin r = net_pins_DDR[p];
        const PinOffset o = pin_off_DDR[p];
        const coord_t   c = node_pos_DDR[r.node_idx];   // random READ-ONLY gather
        r.x = c.x + o.off_x;
        r.y = c.y + o.off_y;
        net_pins_DDR[p] = r;
    }
}

// NODE-major refresh. node_pins is sorted ascending by node_idx (Packer.cpp), so node_idx is
// MONOTONE and this gather is forward-only with good page locality -- materially cheaper than
// the net-major one above, which is why the two are separate loops rather than one parametrized
// pass: they have different memory behaviour and want different port settings. Meow.
static void refresh_node_pins(const coord_t*   node_pos_DDR,     // [num_nodes] current probe v_k
                              const PinOffset* node_pin_off_DDR, // [num_node_pins] static
                              NodePin*         node_pins_DDR,    // [num_node_pins] in place
                              int              num_node_pins) {
refresh_node:
    for (int p = 0; p < num_node_pins; p++) {
#pragma HLS PIPELINE II=1
        NodePin r = node_pins_DDR[p];
        const PinOffset o = node_pin_off_DDR[p];
        const coord_t   c = node_pos_DDR[r.node_idx];   // monotone, forward-only
        r.x = c.x + o.off_x;
        r.y = c.y + o.off_y;
        node_pins_DDR[p] = r;
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_GRADIENT_HPP
