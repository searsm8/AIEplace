#ifndef PL_ALGO_REFRESH_PIN_POS_HPP
#define PL_ALGO_REFRESH_PIN_POS_HPP

// Pin-position refresh (PL).
//
// Folds the current node positions into the pin arrays, so every downstream pass reads a pin's
// absolute position straight out of its NodePin record instead of gathering node_pos[node_idx].
// This is P2 in REPORT_20_hpwl_gradient_opt_20260828.
//
// WHY THIS EXISTS AS ITS OWN PASS. hpwl_gradient phases 1/2/3 and metrics all re-derived the
// same pin position, which put a RANDOM DDR gather in the inner loop of four float pipelines.
// Those gathers are the measured bottleneck (REPORT_20 §2.2: gmem0 is one of only three ports
// HLS could infer no burst for; §2.5 estimates the module runs ~4.7x above its compute floor
// because of them). Concentrating the gather HERE buys three things the inline version cannot:
//   - it happens ONCE per iteration instead of three times;
//   - the loop has no dependent float datapath, so a stalled read stalls nothing but itself,
//     and the port can be tuned (deep outstanding queues) purely for gather throughput;
//   - it is a self-contained producer, so it can later overlap with the density solve in a
//     DATAFLOW region -- impossible for a gather buried in an accumulator recurrence.
//
// ⚠️ MUST RUN BEFORE THE GRADIENT, AT THE SAME PROBE. The arrays it writes are per-iteration
// state carrying v_k. Running the gradient without refreshing first evaluates it at stale
// positions, which does not crash and does not look wrong -- it just silently optimizes the
// previous iterate. See host_interface.hpp NodePin.
//
// (Memory suffix = location: _DDR off chip; bare names are registers.)

#include "../host_interface.hpp"

namespace plalgo {

// NET-major refresh. node_idx is arbitrary here (pins are ordered by net), so node_pos_DDR is a
// TRUE random gather -- the one that survives P2, and the reason gmem0 carries a deep
// outstanding-request queue (top.cpp). Everything else is sequential and burstable: the record
// is read, patched and written back in place.
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
// pass: they have different memory behaviour and want different port settings.
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

#endif // PL_ALGO_REFRESH_PIN_POS_HPP
