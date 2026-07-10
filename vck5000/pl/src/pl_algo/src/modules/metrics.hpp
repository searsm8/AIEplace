#ifndef PL_ALGO_METRICS_HPP
#define PL_ALGO_METRICS_HPP

// metrics -- Stage 5c: reduce the two scalars the host policy needs each iteration.
//   HPWL          = sum over nets of (max_x - min_x) + (max_y - min_y) of the net's pins
//   overflow_sum  = sum over bins of max(0, rho - target_density)
// The host scales overflow_sum by bin_area / total_movable_area to form the ePlace
// overflow ratio (sw_only Grid::computeTotalOverflow: rho*bin_area is the bin overlap,
// capacity is target*bin_area, so excess = bin_area*(rho-target); the bin_area and the
// movable-area normalization stay on the host, matching "host owns the schedule").
// HPWL mirrors DataBase::computeTotalWirelength("HPWL") and hpwl_CU's bbox pass exactly:
// pin position = node_pos[node_idx] + {off_x, off_y}. Totals accumulate in double (a
// float sum over ~1e6 nets/bins is order-dependent to ~0.3%; the metric drives
// convergence, so it must be reproducible), then narrow to float for readback.
//
// node_pos here carries the probe positions v (the same positions the gradient pipeline
// was evaluated at this iteration); the host binds them before this call.

#include "../formats.hpp"
#include "../host_interface.hpp"

namespace plalgo {

static void metrics(const coord_t* node_pos,     // [num_nodes] positions (gmem0)
                    const int*     net_ptr,      // [num_nets+1] CSR offsets (gmem1)
                    const NodePin* pins,         // [num_pins] NET-major (gmem2)
                    const float*   bin_density,  // [GRID*GRID] rho (gmem9)
                    int            num_nets,
                    float          target_density,
                    float*         out)          // out[0]=HPWL, out[1]=overflow_sum (gmem11)
{
    const int num_pins = net_ptr[num_nets];      // CSR end == total pin records

    // ---- HPWL: segmented bounding-box reduce over nets (mirrors hpwl_CU phase A1) ----
    double hpwl_total = 0.0;
    int    cur_net = -1;
    float  maxx = -1e30f, minx = 1e30f, maxy = -1e30f, miny = 1e30f;
hpwl_sweep:
    for (int p = 0; p < num_pins; p++) {
#pragma HLS PIPELINE
        const NodePin r = pins[p];
        if (r.net < 0) continue;                 // pin of a no-gradient (degree<=1) net -> HPWL 0
        if (r.net != cur_net) {                  // net boundary -> add previous net's half-perimeter
            if (cur_net >= 0) hpwl_total += (double)((maxx - minx) + (maxy - miny));
            cur_net = r.net;
            maxx = -1e30f; minx = 1e30f; maxy = -1e30f; miny = 1e30f;
        }
        const coord_t c = node_pos[r.node_idx];
        const float x = c.x + r.off_x, y = c.y + r.off_y;
        if (x > maxx) maxx = x;
        if (x < minx) minx = x;
        if (y > maxy) maxy = y;
        if (y < miny) miny = y;
    }
    if (cur_net >= 0) hpwl_total += (double)((maxx - minx) + (maxy - miny));  // flush last net

    // ---- overflow_sum = sum_bins max(0, rho - target) (host scales to the overflow ratio) ----
    double ovfl_total = 0.0;
ovfl_sweep:
    for (int b = 0; b < N_BINS; b++) {
#pragma HLS PIPELINE
        const float excess = bin_density[b] - target_density;
        if (excess > 0.0f) ovfl_total += (double)excess;
    }

    out[0] = (float)hpwl_total;
    out[1] = (float)ovfl_total;
}

} // namespace plalgo

#endif // PL_ALGO_METRICS_HPP
