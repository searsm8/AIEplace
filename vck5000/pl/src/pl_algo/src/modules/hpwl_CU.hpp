#ifndef PL_ALGO_HPWL_CU_HPP
#define PL_ALGO_HPWL_CU_HPP

// HPWL compute unit (PL).
//
// Computes the weighted-average HPWL gradient (dW/dx, dW/dy) for every movable
// node, entirely on the PL -- no AIE. Math mirrors markv1 computeHpwlPartials_CPU,
// with exp() replaced by a host-supplied LUT (exp(-d/gamma), linear-interpolated).
//
// FLATTENED + NET-TILED SCHEDULE: the work is flat sweeps over pins (not passes
// per net), so each pipeline's fill (iteration latency) is paid once over many
// pins instead of once per ~3-pin net (which was fill-dominated). Per-net
// reductions live in on-chip accumulators indexed by net (bb[]/sums[]); the
// per-pin gradient accumulates into ng[] indexed by node. Each pin carries its
// net id (PinRecord.net, -1 if no gradient), so no separate pin->net map is needed.
//
// NET TILING bounds the on-chip per-net accumulators independent of design size:
// we process nets in windows of TILE_NETS. Because pins are net-major, a net
// window [net0,net1) is a CONTIGUOUS pin range [net_ptr[net0], net_ptr[net1]), so
// each tile is just a flat sweep over that sub-range -- zero redundant work (every
// net is in exactly one tile). bb[]/sums[] are sized to the window and cleared per
// tile; ng[] is the whole-design output, cleared/drained ONCE and accumulated
// across tiles. Per tile:
//   1. sweep_bbox    : scatter min/max into bb[net-net0]
//   2. sweep_sums    : read final bb[], scatter B/C sums into sums[net-net0]
//   3. sweep_partials: read bb[]/sums[], scatter WA partial into ng[idx]
// (ng overflow -- num_movable beyond URAM -- is the second-level node-tiling
// fallback, not yet implemented.)
//
// NOTE (accuracy watch): the LUT is an approximation of exp. Expected to be
// harmless (small per-iteration errors don't accumulate across the solve), but to
// be verified against the algorithm's convergence once the loop is on the PL.
//
// gamma arrives as host-set scalars (inv_gamma for the formula, inv_lut_step for
// the LUT index); the normalized LUT table is uploaded once by the host (only the
// scalars change as the schedule updates gamma).

#include "host_interface.hpp"

namespace plalgo {

constexpr int HPWL_CU_LUT_MAX = 1024;  // max LUT entries cached on-chip

// On-chip accumulator capacities.
//  - TILE_NETS bounds the per-net accumulators (bb[]/sums[]) INDEPENDENT of how
//    many nets the design has: nets are processed in windows of this size. Set
//    small here (8192) to exercise tiling -- mgc_pci_bridge32_b's 29417 nets ->
//    4 tiles. A window still holds ~8192*avg_deg pins, so sweep trip >> latency
//    (fill stays amortized). Raise for fewer tiles once validated.
//  - MAX_MOVABLE bounds ng[] (the whole-design output); num_movable beyond this
//    needs the second-level node-tiling fallback (not yet implemented).
constexpr int HPWL_CU_TILE_NETS   = 1 << 13;  //   8192 nets per tile -> bb[]/sums[] depth
constexpr int HPWL_CU_MAX_MOVABLE = 1 << 18;  // 262144 nodes        -> ng[] depth (2 MB URAM)

// Per-net reduction accumulators (one slot per net, scattered into by flat sweeps).
struct NetBBox { float mxx, mnx, mxy, mny; };                    // bounding box
struct NetSums { float Bpx, Bmx, Cpx, Cmx, Bpy, Bmy, Cpy, Cmy; }; // WA B/C sums

// exp(-d/gamma) via the cached LUT (d >= 0). Beyond the table -> ~0 (underflow).
// Force-inline: the sweeps call this 4x/iteration. Left as a shared instance, HLS
// serializes those 4 calls through one unit (14-cycle latency each -> II ~60).
// Inlined, they become 4 independent datapaths that pipeline off the recurrence.
static inline float hpwl_lut_exp(const float lut[HPWL_CU_LUT_MAX], int lut_size,
                                 float inv_lut_step, float d) {
#pragma HLS INLINE
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
    // ---- on-chip storage --------------------------------------------------
    // Cache the LUT on-chip (avoids a DDR access per exp lookup).
    float lut[HPWL_CU_LUT_MAX];
cache_lut:
    for (int i = 0; i < lut_size; i++) {
#pragma HLS PIPELINE II=1 // II = Initiation Interval (clocks between launching new pipeline inputs)
        lut[i] = exp_lut[i];
    }

    // Per-net reduction accumulators, sized to ONE net-tile window (not num_nets).
    // Scattered into by the per-tile sweeps; cleared per tile.
    static NetBBox bb[HPWL_CU_TILE_NETS];
    static NetSums sums[HPWL_CU_TILE_NETS];
#pragma HLS bind_storage variable=bb   type=RAM_2P impl=URAM
#pragma HLS bind_storage variable=sums type=RAM_2P impl=URAM

    // On-chip per-node gradient accumulator (URAM), the whole-design output. The
    // partials scatter is a read-modify-write with a real loop-carried dependence
    // (a node appears on many pins -> repeated idx), so we cannot DEPENDENCE-false
    // it; keeping the accumulator on-chip shrinks the RMW round-trip from a DDR
    // latency (~141) to a few cycles. ng PERSISTS across tiles -> cleared once
    // here, accumulated by every tile, drained to DDR once at the end.
    static coord_t ng[HPWL_CU_MAX_MOVABLE];
#pragma HLS bind_storage variable=ng type=RAM_2P impl=URAM

clear_grad:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        coord_t z; z.x = 0.0f; z.y = 0.0f;
        ng[n] = z;
    }

    const int num_tiles = (num_nets + HPWL_CU_TILE_NETS - 1) / HPWL_CU_TILE_NETS;

net_tile:
    for (int t = 0; t < num_tiles; t++) {
        const int net0 = t * HPWL_CU_TILE_NETS;
        const int net1 = (net0 + HPWL_CU_TILE_NETS < num_nets)
                       ?  net0 + HPWL_CU_TILE_NETS : num_nets;
        const int pbeg = net_ptr[net0];   // net-major -> tile's pins are contiguous
        const int pend = net_ptr[net1];

        // Clear this tile's per-net accumulators (window-local slots).
    clear_nets:
        for (int j = 0; j < HPWL_CU_TILE_NETS; j++) {
#pragma HLS PIPELINE II=1
            NetBBox b; b.mxx = -1e30f; b.mnx = 1e30f; b.mxy = -1e30f; b.mny = 1e30f;
            bb[j] = b;
            NetSums s{}; sums[j] = s;
        }

        // ---- sweep 1: bounding box (flat over this tile's pins) ----------------
        // Scatter each pin's coord into its net's min/max slot (local index
        // net-net0). The RMW on bb[] is loop-carried (same-net pins are
        // consecutive) but on-chip, so II is a few cycles -- paid once over the
        // tile's pin count (>> latency), not once per net.
    sweep_bbox:
        for (int p = pbeg; p < pend; p++) {
#pragma HLS PIPELINE
            const int net = pins[p].net;
            if (net < 0) continue;
            const int j = net - net0;
            const PinRecord r = pins[p];
            const coord_t   c = node_pos[r.node_idx];
            const float x = c.x + r.off_x;
            const float y = c.y + r.off_y;
            NetBBox b = bb[j];
            if (x > b.mxx) b.mxx = x;
            if (x < b.mnx) b.mnx = x;
            if (y > b.mxy) b.mxy = y;
            if (y < b.mny) b.mny = y;
            bb[j] = b;
        }

        // ---- sweep 2: B/C sums (flat over this tile's pins) --------------------
        // bb[] is final now; accumulate the WA exp-weighted B/C sums per net.
    sweep_sums:
        for (int p = pbeg; p < pend; p++) {
#pragma HLS PIPELINE
            const int net = pins[p].net;
            if (net < 0) continue;
            const int j = net - net0;
            const PinRecord r = pins[p];
            const coord_t   c = node_pos[r.node_idx];
            const float x = c.x + r.off_x;
            const float y = c.y + r.off_y;
            const NetBBox b = bb[j];
            const float apx = hpwl_lut_exp(lut, lut_size, inv_lut_step, b.mxx - x);
            const float amx = hpwl_lut_exp(lut, lut_size, inv_lut_step, x - b.mnx);
            const float apy = hpwl_lut_exp(lut, lut_size, inv_lut_step, b.mxy - y);
            const float amy = hpwl_lut_exp(lut, lut_size, inv_lut_step, y - b.mny);
            NetSums s = sums[j];
            s.Bpx += apx; s.Bmx += amx; s.Cpx += apx * x; s.Cmx += amx * x;
            s.Bpy += apy; s.Bmy += amy; s.Cpy += apy * y; s.Cmy += amy * y;
            sums[j] = s;
        }

        // ---- sweep 3: per-pin partial + scatter into ng (flat over tile pins) --
    sweep_partials:
        for (int p = pbeg; p < pend; p++) {
#pragma HLS PIPELINE
            const int net = pins[p].net;
            if (net < 0) continue;
            const int j = net - net0;
            const PinRecord r = pins[p];
            const int idx = r.node_idx;
            if (idx >= num_movable) continue;  // fixed node: counted in B/C, no grad
            const coord_t c = node_pos[idx];
            const float x = c.x + r.off_x;
            const float y = c.y + r.off_y;
            const NetBBox b = bb[j];
            const NetSums s = sums[j];
            const float apx = hpwl_lut_exp(lut, lut_size, inv_lut_step, b.mxx - x);
            const float amx = hpwl_lut_exp(lut, lut_size, inv_lut_step, x - b.mnx);
            const float apy = hpwl_lut_exp(lut, lut_size, inv_lut_step, b.mxy - y);
            const float amy = hpwl_lut_exp(lut, lut_size, inv_lut_step, y - b.mny);
            const float bpx2 = 1.0f / (s.Bpx * s.Bpx);
            const float bmx2 = 1.0f / (s.Bmx * s.Bmx);
            const float bpy2 = 1.0f / (s.Bpy * s.Bpy);
            const float bmy2 = 1.0f / (s.Bmy * s.Bmy);
            const float px = ((1.0f + x * inv_gamma) * s.Bpx - s.Cpx * inv_gamma) * (apx * bpx2)
                           - ((1.0f - x * inv_gamma) * s.Bmx + s.Cmx * inv_gamma) * (amx * bmx2);
            const float py = ((1.0f + y * inv_gamma) * s.Bpy - s.Cpy * inv_gamma) * (apy * bpy2)
                           - ((1.0f - y * inv_gamma) * s.Bmy + s.Cmy * inv_gamma) * (amy * bmy2);
            coord_t g = ng[idx];
            g.x += px; g.y += py;
            ng[idx] = g;
        }
    }

    // Stream the on-chip accumulator out to DDR once (sequential -> II=1, burst).
write_grad:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        node_grad[n] = ng[n];
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_CU_HPP
