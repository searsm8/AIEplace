#ifndef PL_ALGO_HPWL_CU_HPP
#define PL_ALGO_HPWL_CU_HPP

// HPWL compute unit (PL).
//
// Computes the weighted-average HPWL gradient (dW/dx, dW/dy) for every movable
// node, entirely on the PL -- no AIE. Math mirrors markv1 computeHpwlPartials_CPU,
// with exp() replaced by a host-supplied LUT (exp(-d/gamma), linear-interpolated).
//
// TWO-ORDERING, FULLY ARBITRARY-SIZE SCHEDULE. The kernel runs in two phases, each
// streaming pins in the ordering that suits it (memory name suffix = where it
// lives: _DDR off-chip, _URAM/_BRAM on-chip):
//
//   PHASE A -- net reductions (NET-major, net-tiled).  Over pins_DDR (CSR order)
//   compute each net's bbox + B/C sums in on-chip accumulators bb_URAM/sums_URAM,
//   then SPILL the finished net window to bb_DDR/sums_DDR. Nets are processed in
//   windows of TILE_NETS so the on-chip accumulators stay bounded regardless of
//   num_nets (a net window is a contiguous pin range -- net-major -- so no
//   redundant work). This is the only phase that SCATTERS (into a net histogram),
//   hence the only one that needs on-chip accumulators + tiling.
//
//   PHASE B -- per-node gradient (NODE-major, segmented reduction).  Stream
//   npins_DDR (movable pins sorted by node), read each pin's net reduction from
//   bb_DDR/sums_DDR (random but READ-ONLY -> II=1, latency hidden), accumulate the
//   WA partial in a register, and write node_grad_DDR once when the node changes.
//   No scatter -> no on-chip output accumulator, no node-tiling: node_grad lives in
//   DDR (unbounded num_movable) and is written exactly once per node, in node order
//   (-> sequential, burst-friendly). The contiguity of each node's pins (the host's
//   node-major sort) is what turns the scatter into a write-once reduction.
//
// Net-tiling bounds num_nets (phase A); node-major + DDR output bounds num_movable
// (phase B) -- together, arbitrary design size with no redundant re-sweep.
//
// NOTE (accuracy watch): the LUT is an approximation of exp. Expected harmless
// (small per-iteration errors don't accumulate across the solve), to be re-checked
// against convergence once the full iteration loop is on the PL.

#include "host_interface.hpp"

namespace plalgo {

constexpr int HPWL_CU_LUT_MAX = 1024;  // max LUT entries cached on-chip

// TILE_NETS bounds the on-chip per-net accumulators (bb_URAM/sums_URAM) independent
// of num_nets: nets are processed in windows of this size. Small here (8192) to
// exercise tiling -- mgc_pci_bridge32_b's 29417 nets -> 4 windows. A window holds
// ~8192*avg_deg pins, so the phase-A sweep trip >> latency (fill amortized).
constexpr int HPWL_CU_TILE_NETS = 1 << 13;  // 8192 nets per window

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
                    const int*     net_ptr_DDR,    // [num_nets+1] CSR
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
    // Cache the LUT on-chip (avoids a DDR access per exp lookup).
    float lut_BRAM[HPWL_CU_LUT_MAX];
cache_lut:
    for (int i = 0; i < lut_size; i++) {
#pragma HLS PIPELINE II=1
        lut_BRAM[i] = exp_lut_DDR[i];
    }

    // On-chip per-net accumulators, sized to ONE net-tile window (not num_nets).
    static NetBBox bb_URAM[HPWL_CU_TILE_NETS];
    static NetSums sums_URAM[HPWL_CU_TILE_NETS];
#pragma HLS bind_storage variable=bb_URAM   type=RAM_2P impl=URAM
#pragma HLS bind_storage variable=sums_URAM type=RAM_2P impl=URAM

    // ======================= PHASE A: net reductions =======================
    const int num_net_tiles = (num_nets + HPWL_CU_TILE_NETS - 1) / HPWL_CU_TILE_NETS;

net_tile:
    for (int t = 0; t < num_net_tiles; t++) {
        const int net0 = t * HPWL_CU_TILE_NETS;
        const int net1 = (net0 + HPWL_CU_TILE_NETS < num_nets)
                       ?  net0 + HPWL_CU_TILE_NETS : num_nets;
        const int pbeg = net_ptr_DDR[net0];   // net-major -> tile's pins are contiguous
        const int pend = net_ptr_DDR[net1];

    clear_nets:
        for (int j = 0; j < HPWL_CU_TILE_NETS; j++) {
#pragma HLS PIPELINE II=1
            NetBBox b; b.mxx = -1e30f; b.mnx = 1e30f; b.mxy = -1e30f; b.mny = 1e30f;
            bb_URAM[j] = b;
            NetSums s{}; sums_URAM[j] = s;
        }

        // sweep 1: bounding box (scatter min/max into the net's window slot).
    sweep_bbox:
        for (int p = pbeg; p < pend; p++) {
#pragma HLS PIPELINE
            const int net = pins_DDR[p].net;
            if (net < 0) continue;
            const int j = net - net0;
            const NodePin r = pins_DDR[p];
            const coord_t c = node_pos_DDR[r.node_idx];
            const float x = c.x + r.off_x;
            const float y = c.y + r.off_y;
            NetBBox b = bb_URAM[j];
            if (x > b.mxx) b.mxx = x;
            if (x < b.mnx) b.mnx = x;
            if (y > b.mxy) b.mxy = y;
            if (y < b.mny) b.mny = y;
            bb_URAM[j] = b;
        }

        // sweep 2: B/C sums (bb_URAM is final; accumulate exp-weighted sums).
    sweep_sums:
        for (int p = pbeg; p < pend; p++) {
#pragma HLS PIPELINE
            const int net = pins_DDR[p].net;
            if (net < 0) continue;
            const int j = net - net0;
            const NodePin r = pins_DDR[p];
            const coord_t c = node_pos_DDR[r.node_idx];
            const float x = c.x + r.off_x;
            const float y = c.y + r.off_y;
            const NetBBox b = bb_URAM[j];
            const float apx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, b.mxx - x);
            const float amx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, x - b.mnx);
            const float apy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, b.mxy - y);
            const float amy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, y - b.mny);
            NetSums s = sums_URAM[j];
            s.Bpx += apx; s.Bmx += amx; s.Cpx += apx * x; s.Cmx += amx * x;
            s.Bpy += apy; s.Bmy += amy; s.Cpy += apy * y; s.Cmy += amy * y;
            sums_URAM[j] = s;
        }

        // spill the finished window to DDR so phase B can read any net's reduction.
    spill:
        for (int j = 0; j < net1 - net0; j++) {
#pragma HLS PIPELINE II=1
            bb_DDR[net0 + j]   = bb_URAM[j];
            sums_DDR[net0 + j] = sums_URAM[j];
        }
    }

    // ===================== PHASE B: per-node gradient ======================
    // node_grad is written once per node; nodes with no gradient-bearing pin never
    // appear in npins, so zero the output first (sequential -> burst).
clear_grad:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        coord_t z; z.x = 0.0f; z.y = 0.0f;
        node_grad_DDR[n] = z;
    }

    // Segmented reduction: accumulate a node's WA partials in a register, flush to
    // node_grad_DDR on the node boundary. No RMW on the output -> write-once.
    // (acc is float here -> the carried add bounds II at the fadd latency; a
    // fixed-point accumulator is the II=1 follow-up.)
    int   cur_node = -1;
    float ax = 0.0f, ay = 0.0f;
seg_reduce:
    for (int p = 0; p < num_npins; p++) {
#pragma HLS PIPELINE
        const NodePin r = npins_DDR[p];
        if (r.node_idx != cur_node) {              // segment boundary
            if (cur_node >= 0) {
                coord_t g; g.x = ax; g.y = ay;
                node_grad_DDR[cur_node] = g;        // flush previous node
            }
            cur_node = r.node_idx; ax = 0.0f; ay = 0.0f;
        }
        const coord_t c = node_pos_DDR[r.node_idx];
        const float x = c.x + r.off_x;
        const float y = c.y + r.off_y;
        const NetBBox b = bb_DDR[r.net];            // random READ-ONLY -> II=1
        const NetSums s = sums_DDR[r.net];
        const float apx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, b.mxx - x);
        const float amx = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, x - b.mnx);
        const float apy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, b.mxy - y);
        const float amy = hpwl_lut_exp(lut_BRAM, lut_size, inv_lut_step, y - b.mny);
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
    if (cur_node >= 0) {                            // flush the last node
        coord_t g; g.x = ax; g.y = ay;
        node_grad_DDR[cur_node] = g;
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_CU_HPP
