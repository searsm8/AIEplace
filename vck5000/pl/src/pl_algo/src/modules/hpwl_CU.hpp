#ifndef PL_ALGO_HPWL_CU_HPP
#define PL_ALGO_HPWL_CU_HPP

// HPWL compute unit (PL).
//
// Computes the weighted-average HPWL gradient (dW/dx, dW/dy) for every movable
// node, entirely on the PL -- no AIE. Math mirrors markv1 computeHpwlPartials_CPU,
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
//   PHASE B  -- gradient, segmented over NODES (npins_DDR, node-major sorted).
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

// exp(-d/gamma) via the cached LUT (d >= 0). Beyond the table -> ~0 (underflow).
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
                    const NodePin* pins_DDR,       // [num_pins] NET-major (phase A)
                    const NodePin* npins_DDR,      // [num_npins] NODE-major (phase B)
                    const float*   exp_lut_DDR,    // [lut_size] exp(-t) table
                    NetBBox*       bb_DDR,         // [num_nets] scratch (A writes, B reads)
                    NetSums*       sums_DDR,       // [num_nets] scratch (A writes, B reads)
                    coord_t*       node_grad_DDR,  // [num_movable] gradient (output)
                    float          inv_gamma,
                    float          inv_lut_step,
                    int            lut_size,
                    int            num_nets,
                    int            num_movable,
                    int            num_npins) {
    const int num_pins = net_ptr_DDR[num_nets];   // CSR end == total pin records

    // Cache the LUT on-chip (avoids a DDR access per exp lookup).
    float lut_BRAM[HPWL_CU_LUT_MAX];
cache_lut:
    for (int i = 0; i < lut_size; i++) {
#pragma HLS PIPELINE II=1
        lut_BRAM[i] = exp_lut_DDR[i];
    }

    // ===== PHASE A1: bounding box, segmented over nets (register-accumulated) =====
    int   bb_net = -1;                              // current segment's net
    float maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
sweep_bbox:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE
        const NodePin r = pins_DDR[p];
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

    // ===== PHASE A2: B/C sums, segmented over nets (bb_DDR is final) =====
    int     bc_net = -1;
    NetBBox b{};                                    // current net's bbox (read at boundary)
    float Bpx = 0, Bmx = 0, Cpx = 0, Cmx = 0, Bpy = 0, Bmy = 0, Cpy = 0, Cmy = 0;
sweep_sums:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE
        const NodePin r = pins_DDR[p];
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
        const coord_t c = node_pos_DDR[r.node_idx];
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

    // ===== PHASE B: per-node gradient, segmented over nodes =====
    // node_grad is write-once per node; nodes with no gradient-bearing pin never
    // appear in npins, so zero the output first (sequential -> burst).
clear_grad:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        coord_t z; z.x = 0.0f; z.y = 0.0f;
        node_grad_DDR[n] = z;
    }

    int   cur_node = -1;
    float ax = 0.0f, ay = 0.0f;
seg_reduce:
    for (int p = 0; p < num_npins; p++) {
#pragma HLS PIPELINE
        const NodePin r = npins_DDR[p];
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
