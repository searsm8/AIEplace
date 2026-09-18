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
//   PHASE 2.5 -- per-pin gradient, segmented over NETS (net_pins_DDR again). At each net
//   change read that net's final bb_DDR/sums_DDR (once/net, ascending -> sequential); the WA
//   partial of each pin is a function of the pin's (x,y) and those net scalars alone, so it is
//   computed here in net order and SCATTERED into node-major order via a static permutation
//   (pin_to_npin_DDR). This replaces the old phase B's per-node-pin random gather of
//   bb/sums (12 floats at a random net) with a per-pin random WRITE of the 2-float gradient.
//   PHASE 3  -- reduction, segmented over NODES. pin_grad_DDR is already node-major, so this is
//   a pure sequential stream: sum pin_grad_DDR[p], flush node_grad_DDR[node] at the node change
//   (node_pins_DDR read for node_idx only). Write-once in node order (-> sequential, burst).
//
// bb_DDR/sums_DDR (DDR scratch, [num_nets]) bridge phase 2 -> 2.5; pin_grad_DDR (scratch,
// [num_node_pins]) bridges 2.5 -> 3. node_grad in DDR -> arbitrary num_movable.
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
                    const NodePin* net_pins_DDR,       // [num_pins] NET-major (phases 1, 2, 2.5)
                    const NodePin* node_pins_DDR,      // [num_node_pins] NODE-major (phase 3: node_idx only)
                    const int*     pin_to_npin_DDR,    // [num_pins] net-major -> node-major slot, -1 if none
                    const float*   exp_lut_DDR,    // [lut_size] exp(-t) table
                    NetBBox*       bb_DDR,         // [num_nets] scratch (A writes, B reads)
                    NetSums*       sums_DDR,       // [num_nets] scratch (A writes, B reads)
                    coord_t*       pin_grad_DDR,   // [num_node_pins] scratch (2.5 scatters, 3 reduces)
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
                NetBBox b; b.max_x = maxx; b.min_x = minx; b.max_y = maxy; b.min_y = miny;
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
        NetBBox b; b.max_x = maxx; b.min_x = minx; b.max_y = maxy; b.min_y = miny;
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
                hpwl_part[k] += (double)((b.max_x - b.min_x) + (b.max_y - b.min_y));
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
        const float apx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, b.max_x - x);
        const float amx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, x - b.min_x);
        const float apy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, b.max_y - y);
        const float amy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, y - b.min_y);
        Bpx += apx; Bmx += amx; Cpx += apx * x; Cmx += amx * x;
        Bpy += apy; Bmy += amy; Cpy += apy * y; Cmy += amy * y;
    }
    if (bc_net >= 0) {                              // flush last net
        NetSums s; s.Bpx = Bpx; s.Bmx = Bmx; s.Cpx = Cpx; s.Cmx = Cmx;
                  s.Bpy = Bpy; s.Bmy = Bmy; s.Cpy = Cpy; s.Cmy = Cmy;
        sums_DDR[bc_net] = s;
    }

    // ===== PHASE 2.5: per-pin gradient in NET-major order, scattered to node order =====
    // The per-pin WA partial needs only the pin's own (x,y) plus its net's FINAL bbox and B/C
    // sums -- all net-level scalars, so each pin's gradient is independent once the net is closed.
    // Computing it HERE, net-major, means bb_DDR/sums_DDR are read once per net at the boundary,
    // sequentially in ascending net order (burstable) -- NOT the per-node-pin random gather the
    // old phase 3 did (a NetBBox+NetSums = 12 floats fetched at a random r.net for EVERY node
    // pin). The result is written out via a static permutation into node-major order, so the
    // reduction that follows is a pure sequential stream. Net memory change: one 12-float random
    // READ per node-pin -> one 2-float random WRITE per pin. Same transaction count, ~6x fewer
    // bytes, and a write (fire-and-forget, no return latency on the datapath) instead of a read.
    //
    // The scatter target pin_to_npin_DDR[p] is the node-major slot of net-major pin p, or -1 if
    // the pin has no gradient slot (masked net, or a fixed node with node_idx >= num_movable). It
    // is a STATIC permutation (the net set never changes) built once by the host from the same
    // node-major sort Packer already does; each live pin maps to a DISTINCT slot, so the write is
    // injective and write-only -> no read-modify-write recurrence, II=1. Meow.
    int     pg_net = -1;
    NetBBox pgb{};                                   // current net's final bbox
    NetSums pgs{};                                   // current net's final B/C sums
    float   pg_bpx2 = 0, pg_bmx2 = 0, pg_bpy2 = 0, pg_bmy2 = 0;   // 1/B^2, per net (const over pins)
pin_grad:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE
        const NodePin r = net_pins_DDR[p];          // sequential (one big burstable block)
        if (r.net < 0) continue;                    // masked net: no gradient
        if (r.net != pg_net) {                       // net boundary -> load finals (once/net)
            pg_net = r.net;
            pgb = bb_DDR[r.net];                     // ascending net order -> sequential/burstable
            pgs = sums_DDR[r.net];
            pg_bpx2 = 1.0f / (pgs.Bpx * pgs.Bpx);
            pg_bmx2 = 1.0f / (pgs.Bmx * pgs.Bmx);
            pg_bpy2 = 1.0f / (pgs.Bpy * pgs.Bpy);
            pg_bmy2 = 1.0f / (pgs.Bmy * pgs.Bmy);
        }
        const int slot = pin_to_npin_DDR[p];         // sequential read of the permutation
        if (slot < 0) continue;                      // fixed-node pin on a live net: no grad slot
        const float x = r.x;                         // absolute position, already folded in
        const float y = r.y;
        const float apx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, pgb.max_x - x);
        const float amx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, x - pgb.min_x);
        const float apy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, pgb.max_y - y);
        const float amy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, y - pgb.min_y);
        const float px = ((1.0f + x * inv_gamma) * pgs.Bpx - pgs.Cpx * inv_gamma) * (apx * pg_bpx2)
                       - ((1.0f - x * inv_gamma) * pgs.Bmx + pgs.Cmx * inv_gamma) * (amx * pg_bmx2);
        const float py = ((1.0f + y * inv_gamma) * pgs.Bpy - pgs.Cpy * inv_gamma) * (apy * pg_bpy2)
                       - ((1.0f - y * inv_gamma) * pgs.Bmy + pgs.Cmy * inv_gamma) * (amy * pg_bmy2);
        coord_t g; g.x = px; g.y = py;
        pin_grad_DDR[slot] = g;                      // SCATTER: random write, injective -> II=1
    }

    // ===== PHASE 3: per-node reduction, now fully SEQUENTIAL =====
    // pin_grad_DDR is laid out in node-major order (phase 2.5 scattered through the same
    // permutation node_pins is sorted by), so pin_grad_DDR[p] is exactly the gradient of
    // node_pins_DDR[p]. Summing them in this order reproduces the old seg_reduce accumulation
    // order EXACTLY -- the result is bit-identical, only the random reads are gone. node_pins is
    // still read, but for node_idx alone (the segment key); its x/y/net are now unused here.
    // node_grad is write-once per node; nodes with no gradient-bearing pin never appear, so zero
    // the output first (sequential -> burst).
clear_grad:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        coord_t z; z.x = 0.0f; z.y = 0.0f;
        node_grad_DDR[n] = z;
    }

    int   cur_node = -1;
    float ax = 0.0f, ay = 0.0f;
node_reduce:
    for (int p = 0; p < num_node_pins; p++) {
#pragma HLS PIPELINE
        const int     node = node_pins_DDR[p].node_idx;   // sequential (segment key)
        const coord_t g    = pin_grad_DDR[p];             // sequential (node-major layout)
        if (node != cur_node) {                     // node boundary -> flush previous
            if (cur_node >= 0) {
                coord_t o; o.x = ax; o.y = ay;
                node_grad_DDR[cur_node] = o;
            }
            cur_node = node; ax = 0.0f; ay = 0.0f;
        }
        ax += g.x; ay += g.y;
    }
    if (cur_node >= 0) {                             // flush last node
        coord_t o; o.x = ax; o.y = ay;
        node_grad_DDR[cur_node] = o;
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
