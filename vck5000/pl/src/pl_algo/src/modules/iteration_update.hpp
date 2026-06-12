#ifndef PL_ALGO_ITERATION_UPDATE_HPP
#define PL_ALGO_ITERATION_UPDATE_HPP

// Iteration Update module (black box for v1).
//
// Role: turn gradients + fields into the next node positions. Internally (defined later)
// this gathers the E-field at each node (bilinear), combines gradients g = g_wl + lambda*g_d,
// applies the Barzilai-Borwein step alpha, the Nesterov update, and die-boundary clamping.
// For this deliverable it is defined purely by its I/O contract.
//
// It does NOT write the coordinate buffer directly: it streams the updated coordinates to
// the Memory Writer (the single DDR coords writer). It does own the Nesterov state buffer.
//
// Inputs:
//   node_grad : DDR per-node HPWL gradient (from HPWL Manager)
//   efield_x  : DDR Ex field (from Density Manager)
//   efield_y  : DDR Ey field (from Density Manager)
//   coords    : DDR current node coordinates (read)
//   lambda    : density weight (host policy)
//   alpha     : Barzilai-Borwein step length
//   die_*     : placement region bounds for clamping
//   num_nodes : movable node count
// Outputs:
//   node_state : DDR Nesterov state (read-modify-write)
//   coords_out : stream of updated coordinates -> Memory Writer

#include "../formats.hpp"

namespace plalgo {

static void iteration_update(const beat_t* node_grad,
                             const beat_t* efield_x,
                             const beat_t* efield_y,
                             const beat_t* coords,
                             beat_t* node_state,
                             float lambda,
                             float alpha,
                             float die_xmin, float die_xmax,
                             float die_ymin, float die_ymax,
                             int num_nodes,
                             hls::stream<axis_t>& coords_out) {
    // TODO: field gather + gradient combine + BB step + Nesterov update + clamp.
    // Stub: pass current coordinates straight through to the Memory Writer so the
    // dataflow edge (and every port) is exercised for synthesis.
passthrough:
    for (int n = 0; n < num_nodes; n++) {
        axis_t beat;
        beat.data = coords[n];
        beat.keep_all();
        coords_out.write(beat);
    }
    (void)node_grad;
    (void)efield_x;
    (void)efield_y;
    (void)node_state;
    (void)lambda;
    (void)alpha;
    (void)die_xmin; (void)die_xmax; (void)die_ymin; (void)die_ymax;
}

} // namespace plalgo

#endif // PL_ALGO_ITERATION_UPDATE_HPP
