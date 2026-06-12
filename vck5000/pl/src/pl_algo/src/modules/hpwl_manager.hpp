#ifndef PL_ALGO_HPWL_MANAGER_HPP
#define PL_ALGO_HPWL_MANAGER_HPP

// HPWL Manager module (black box for v1).
//
// Role: drive the AIE HPWL gradient graph. Walk the net/pin connectivity, gather pin
// coordinates from the coords buffer, pack net groups, stream them to the AIE graph,
// receive per-pin partials (dW/dx, dW/dy), and scatter-accumulate them into the
// per-node HPWL gradient buffer. A node appears in many nets, so the write-back is an
// accumulate, not a store.
//
// Inputs:
//   coords      : DDR node coordinates (read)
//   net_data    : DDR net/pin connectivity (read, see formats.hpp)
//   gamma       : wirelength smoothing parameter (set by host policy)
//   num_nodes   : movable node count
//   num_nets    : net count
//   hpwl_from_aie : per-pin partials returned by the AIE HPWL graph
// Outputs:
//   node_grad   : DDR per-node HPWL gradient (accumulated)
//   hpwl_to_aie : pin-coordinate packets sent to the AIE HPWL graph

#include "../formats.hpp"

namespace plalgo {

static void hpwl_manager(const beat_t* coords,
                         const beat_t* net_data,
                         beat_t* node_grad,
                         float gamma,
                         int num_nodes,
                         int num_nets,
                         hls::stream<axis_t>& hpwl_to_aie,
                         hls::stream<axis_t>& hpwl_from_aie) {
    // TODO: pack nets -> AIE HPWL graph, receive partials, scatter-accumulate to nodes.
    // Stub: clear the gradient buffer so the m_axi port is live and well-defined.
clear_grad:
    for (int n = 0; n < num_nodes; n++) {
        node_grad[n] = beat_t(0);
    }
    (void)coords;
    (void)net_data;
    (void)gamma;
    (void)num_nets;
    (void)hpwl_to_aie;
    (void)hpwl_from_aie;
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_MANAGER_HPP
