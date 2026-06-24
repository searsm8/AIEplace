#ifndef PL_ALGO_HPWL_CU_HPP
#define PL_ALGO_HPWL_CU_HPP

// HPWL compute unit (PL).
//
// Computes the weighted-average HPWL gradient (dW/dx, dW/dy) for every movable
// node, entirely on the PL -- no AIE. Math mirrors markv1 computeHpwlPartials_CPU,
// with exp() replaced by a host-supplied LUT (exp(-d/gamma), linear-interpolated).
//
// FLATTENED SCHEDULE: the work is three *flat sweeps over all pins* (not three
// passes per net). Per-net reductions live in on-chip accumulators indexed by net
// (bb[]/sums[]); the per-pin gradient accumulates into ng[] indexed by node. A
// pin->net map lets each sweep run as one flat loop, so each pipeline's fill
// (iteration latency) is paid ONCE over ~num_pins iterations instead of once per
// net -- the earlier per-net structure re-filled gather/reduce/partials ~num_nets
// times for nets averaging ~3 pins, which was fill-dominated. Sweeps:
//   0. build_pin2net : pin -> its net (or -1 if the net has no gradient)
//   1. sweep_bbox    : scatter min/max into bb[net]
//   2. sweep_sums    : read final bb[net], scatter B/C sums into sums[net]
//   3. sweep_partials: read bb[net]+sums[net], scatter WA partial into ng[idx]
// Because reductions are per-net accumulators (no per-net pin buffer), there is no
// per-net degree cap -- nets of any degree are handled.
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

// On-chip accumulator capacities. These bound how large a design fits fully on
// chip; beyond them the design needs the tiled / scatter-to-unique follow-on (the
// same bounded-slow path as the >URAM node_grad case). Current benchmarks are well
// under all three (e.g. mgc_pci_bridge32_b: nets 29417, pins 83944, movable 28914).
constexpr int HPWL_CU_MAX_NETS    = 1 << 16;  //  65536 nets  -> bb[]/sums[] depth
constexpr int HPWL_CU_MAX_PINS    = 1 << 18;  // 262144 pins  -> pin2net[] depth
constexpr int HPWL_CU_MAX_MOVABLE = 1 << 18;  // 262144 nodes -> ng[] depth (2 MB URAM)

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
    const int num_pins = net_ptr[num_nets];   // CSR: total pin records

    // ---- on-chip storage --------------------------------------------------
    // Cache the LUT on-chip (avoids a DDR access per exp lookup).
    float lut[HPWL_CU_LUT_MAX];
cache_lut:
    for (int i = 0; i < lut_size; i++) {
#pragma HLS PIPELINE II=1 // II = Initiation Interval (clocks between launching new pipeline inputs)
        lut[i] = exp_lut[i];
    }

    // pin -> net map (or -1 for pins of a no-gradient net). Lets the sweeps below
    // be flat loops over pins with a simple sequential read instead of per-net
    // re-entry. Built once from the CSR.
    static int pin2net[HPWL_CU_MAX_PINS];

    // Per-net reduction accumulators, scattered into by the flat sweeps.
    static NetBBox bb[HPWL_CU_MAX_NETS];
    static NetSums sums[HPWL_CU_MAX_NETS];
#pragma HLS bind_storage variable=bb   type=RAM_2P impl=URAM
#pragma HLS bind_storage variable=sums type=RAM_2P impl=URAM

    // On-chip per-node gradient accumulator (URAM). The partials scatter is a
    // read-modify-write with a real loop-carried dependence (a node appears on many
    // pins -> repeated idx), so we cannot DEPENDENCE-false it; keeping the
    // accumulator on-chip shrinks the RMW round-trip from a DDR latency (~141) to a
    // few cycles. Drained to DDR once at the end (write_grad).
    static coord_t ng[HPWL_CU_MAX_MOVABLE];
#pragma HLS bind_storage variable=ng type=RAM_2P impl=URAM

    // ---- sweep 0: build pin2net, tagging no-gradient (deg<=1) nets as -1 -------
build_pin2net:
    for (int net = 0; net < num_nets; net++) {
        const int beg = net_ptr[net];
        const int end = net_ptr[net + 1];
        const int tag = (end - beg <= 1) ? -1 : net;   // deg<=1 has no gradient
        for (int p = beg; p < end; p++) {
#pragma HLS PIPELINE II=1
            pin2net[p] = tag;
        }
    }

    // ---- clear the accumulators ------------------------------------------------
clear_nets:
    for (int net = 0; net < num_nets; net++) {
#pragma HLS PIPELINE II=1
        NetBBox b; b.mxx = -1e30f; b.mnx = 1e30f; b.mxy = -1e30f; b.mny = 1e30f;
        bb[net] = b;
        NetSums s{}; sums[net] = s;
    }
clear_grad:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        coord_t z; z.x = 0.0f; z.y = 0.0f;
        ng[n] = z;
    }

    // ---- sweep 1: bounding box (flat over all pins) ----------------------------
    // Scatter each pin's coord into its net's min/max accumulator. Same-net pins
    // are consecutive (net-major CSR) -> the RMW on bb[net] is loop-carried, but
    // on-chip so its II is a few cycles, paid once over num_pins (not per net).
sweep_bbox:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE
        const int net = pin2net[p];
        if (net < 0) continue;
        const PinRecord r = pins[p];
        const coord_t   c = node_pos[r.node_idx];
        const float x = c.x + r.off_x;
        const float y = c.y + r.off_y;
        NetBBox b = bb[net];
        if (x > b.mxx) b.mxx = x;
        if (x < b.mnx) b.mnx = x;
        if (y > b.mxy) b.mxy = y;
        if (y < b.mny) b.mny = y;
        bb[net] = b;
    }

    // ---- sweep 2: B/C sums (flat over all pins) --------------------------------
    // bb[] is final now; accumulate the WA exp-weighted B/C sums per net.
sweep_sums:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE
        const int net = pin2net[p];
        if (net < 0) continue;
        const PinRecord r = pins[p];
        const coord_t   c = node_pos[r.node_idx];
        const float x = c.x + r.off_x;
        const float y = c.y + r.off_y;
        const NetBBox b = bb[net];
        const float apx = hpwl_lut_exp(lut, lut_size, inv_lut_step, b.mxx - x);
        const float amx = hpwl_lut_exp(lut, lut_size, inv_lut_step, x - b.mnx);
        const float apy = hpwl_lut_exp(lut, lut_size, inv_lut_step, b.mxy - y);
        const float amy = hpwl_lut_exp(lut, lut_size, inv_lut_step, y - b.mny);
        NetSums s = sums[net];
        s.Bpx += apx; s.Bmx += amx; s.Cpx += apx * x; s.Cmx += amx * x;
        s.Bpy += apy; s.Bmy += amy; s.Cpy += apy * y; s.Cmy += amy * y;
        sums[net] = s;
    }

    // ---- sweep 3: per-pin partial + scatter into ng (flat over all pins) -------
sweep_partials:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE
        const int net = pin2net[p];
        if (net < 0) continue;
        const PinRecord r = pins[p];
        const int idx = r.node_idx;
        if (idx >= num_movable) continue;  // fixed node: counted in B/C, no grad
        const coord_t c = node_pos[idx];
        const float x = c.x + r.off_x;
        const float y = c.y + r.off_y;
        const NetBBox b = bb[net];
        const NetSums s = sums[net];
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

    // Stream the on-chip accumulator out to DDR once (sequential -> II=1, burst).
write_grad:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        node_grad[n] = ng[n];
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_CU_HPP
