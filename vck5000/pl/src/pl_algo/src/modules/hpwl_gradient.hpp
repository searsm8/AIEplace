#ifndef PL_ALGO_HPWL_CU_HPP
#define PL_ALGO_HPWL_CU_HPP

// HPWL compute unit (PL).
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
//   registers; write sums_DDR[net] at the next change. (Two passes, not one: B/C
//   needs the net's final max, and re-streaming pins_DDR from DDR -- sequential,
//   II=1 -- is cheaper than buffering a whole net on chip, and has no degree cap.)
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

constexpr int HPWL_CU_LUT_MAX = 1024;  // max LUT entries cached on-chip

// Partial accumulators for the HPWL by-product sum in phase 1. Power of two so the rotation
// is a mask, not a modulo. 8 spreads dependent double adds >= 16 pins apart -- see sweep_bbox.
constexpr int HPWL_PARTIALS = 8;

// compute exp(-d/gamma) via the cached LUT (d >= 0). Beyond the table -> ~0 (underflow).
// Force-inline (the sweeps call it 4x/iteration): left as a shared instance HLS
// serializes the 4 calls (14-cyc latency each); inlined they pipeline independently.
static inline float hpwl_lut_exp(const float lut_BRAM[HPWL_CU_LUT_MAX], int lut_size,
                                 float inv_lut_step, float d) {
#pragma HLS INLINE
    float idx_f = d * inv_lut_step;
    int   idx   = (int)idx_f;
    if (idx >= lut_size - 1) return 0.0f;
    float frac = idx_f - (float)idx;
    return lut_BRAM[idx] * (1.0f - frac) + lut_BRAM[idx + 1] * frac;
}

static void hpwl_CU(const coord_t* node_pos_DDR,   // [num_nodes] AoS {x,y}
                    const int*     net_ptr_DDR,    // [num_nets+1] CSR (unused: kept for ABI)
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
    float lut_BRAM[HPWL_CU_LUT_MAX];
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
        const coord_t c = node_pos_DDR[r.node_idx];
        const float x = c.x + r.off_x;
        const float y = c.y + r.off_y;
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
    // Unrolled HPWL_PARTIALS-wide with STATIC accumulator indices -- static is what makes each
    // partial a register rather than a memory, so the 8 dependence chains run in parallel.
    // Measured cost of this block: LUT +7941 (+2.4% of device), FF +5321, BRAM 0, timing slack
    // unchanged. Nearly all of that is the 8 parallel double adders, so HPWL_PARTIALS is the
    // knob if LUT gets tight -- halving it roughly halves the adder cost and doubles this
    // loop's cycles, which is noise against the 6.6M it saves.
    //
    // ⚠️ REQUIRES bb_DDR TO BE ZEROED BEFORE FIRST USE. Masked nets are never written by
    // sweep_bbox, so they keep whatever the buffer held; zeroed, they contribute a zero-extent
    // box and drop out of the sum for free. One host-side memset at allocation is enough -- the
    // net set is static, so masked entries stay zero for the whole run. See host_interface.hpp.
    double hpwl_part[HPWL_PARTIALS];
#pragma HLS ARRAY_PARTITION variable=hpwl_part complete dim=1
hpwl_init:
    for (int i = 0; i < HPWL_PARTIALS; i++) {
#pragma HLS UNROLL
        hpwl_part[i] = 0.0;
    }
hpwl_reduce:
    for (int n = 0; n < num_nets; n += HPWL_PARTIALS) {
#pragma HLS PIPELINE
        for (int k = 0; k < HPWL_PARTIALS; k++) {
#pragma HLS UNROLL
            const int idx = n + k;
            if (idx < num_nets) {
                const NetBBox b = bb_DDR[idx];
                hpwl_part[k] += (double)((b.mxx - b.mnx) + (b.mxy - b.mny));
            }
        }
    }
    double hpwl_total = 0.0;
hpwl_combine:
    for (int i = 0; i < HPWL_PARTIALS; i++) hpwl_total += hpwl_part[i];
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
        const coord_t c = node_pos_DDR[r.node_idx]; // Random access, slow but read-only -> II=1
        const float x = c.x + r.off_x;
        const float y = c.y + r.off_y;
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
        const coord_t c = node_pos_DDR[r.node_idx];
        const float x = c.x + r.off_x;
        const float y = c.y + r.off_y;

        // 
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

} // namespace plalgo

#endif // PL_ALGO_HPWL_CU_HPP
