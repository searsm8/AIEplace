#ifndef PL_ALGO_METRICS_HPP
#define PL_ALGO_METRICS_HPP

// Metrics module (black box for v1).
//
// Role: reduce the two scalars the host policy needs each iteration -- total HPWL and
// density overflow. These are read back over AXI-Lite/DDR by the host, which owns the
// gamma/lambda schedule and the convergence test for v1.
//   HPWL     = sum over nets of (max - min) bounding-box half-perimeter
//   overflow = sum over bins of max(0, rho - target) (normalized by movable area, host side)
//
// Inputs:
//   coords      : DDR node coordinates (read)
//   net_data    : DDR net/pin connectivity (read)
//   bin_density : DDR bin density (read, produced by Density Manager)
//   num_nodes   : movable node count
//   num_nets    : net count
// Outputs:
//   out_hpwl     : reduced HPWL scalar
//   out_overflow : reduced overflow scalar

#include "../formats.hpp"

namespace plalgo {

static void metrics(const beat_t* coords,
                    const beat_t* net_data,
                    const beat_t* bin_density,
                    int num_nodes,
                    int num_nets,
                    float* out_hpwl,
                    float* out_overflow) {
    // TODO: per-net min/max HPWL reduce + per-bin overflow reduce.
    // Stub: emit zeros so the output ports are well-defined.
    *out_hpwl     = 0.0f;
    *out_overflow = 0.0f;
    (void)coords;
    (void)net_data;
    (void)bin_density;
    (void)num_nodes;
    (void)num_nets;
}

} // namespace plalgo

#endif // PL_ALGO_METRICS_HPP
