// top.cpp -- pl_algo PL kernel, v0 bring-up: total HPWL on the PL.
//
// v0 scope: read the static design (node positions + CSR net connectivity + pin
// offsets) the host uploads per host_interface.hpp, compute the total
// half-perimeter wirelength, and write one float back. This proves the host->PL
// transfer path end-to-end in sw_emu before any iteration / density / AIE
// machinery is added.
//
// The full PL-centric design (per-iteration FSM wiring the memory_writer /
// hpwl_manager / density_manager / iteration_update / metrics modules, AIE FFT
// pool + HPWL graph streams) is documented in DATAFLOW.md and modules/*.hpp, and
// the earlier interface skeleton of this file is preserved in git history. It is
// reintroduced incrementally on top of this working kernel.
//
// v0 uses natural typed m_axi pointers (coord_t / PinRecord / int / float) rather
// than the 128-bit beat packing in formats.hpp -- the beat layout is a throughput
// optimization for the full design; correctness first here.

#include "host_interface.hpp"

using namespace plalgo;

extern "C" {
void top(
    const coord_t*   node_pos,   // [num_nodes]      movable [0,M), fixed [M,N)
    const int*       net_ptr,    // [num_nets + 1]   CSR prefix offsets into pins
    const PinRecord* pins,       // [num_pins]       flattened, net-major
    float*           result,     // [1]              total HPWL (output)
    int              num_nets)
{
#pragma HLS INTERFACE m_axi port=node_pos offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=net_ptr  offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=pins     offset=slave bundle=gmem2
#pragma HLS INTERFACE m_axi port=result   offset=slave bundle=gmem3
// Each m_axi port's offset register and every scalar arg must share one
// AXI-Lite bundle ("control") in Vitis kernel mode.
#pragma HLS INTERFACE s_axilite port=node_pos bundle=control
#pragma HLS INTERFACE s_axilite port=net_ptr  bundle=control
#pragma HLS INTERFACE s_axilite port=pins     bundle=control
#pragma HLS INTERFACE s_axilite port=result   bundle=control
#pragma HLS INTERFACE s_axilite port=num_nets bundle=control
#pragma HLS INTERFACE s_axilite port=return   bundle=control

    // Accumulate in double: summing ~1e6 per-net values (~1e5 each) into a float
    // is order-dependent to ~0.3%. Per-net bbox stays float (positions are float).
    double total = 0.0;

net_loop:
    for (int n = 0; n < num_nets; n++) {
        const int beg = net_ptr[n];
        const int end = net_ptr[n + 1];
        if (end <= beg) continue;   // empty net contributes 0

        // Seed the bounding box with the first pin.
        const PinRecord r0 = pins[beg];
        const coord_t   c0 = node_pos[r0.node_idx];
        float min_x = c0.x + r0.off_x, max_x = min_x;
        float min_y = c0.y + r0.off_y, max_y = min_y;

    pin_loop:
        for (int p = beg + 1; p < end; p++) {
            const PinRecord r = pins[p];
            const coord_t   c = node_pos[r.node_idx];   // gather by node index
            const float x = c.x + r.off_x;
            const float y = c.y + r.off_y;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
        total += (double)((max_x - min_x) + (max_y - min_y));
    }

    result[0] = (float)total;   // final metric to ~7 sig figs is plenty for the host
}
} // extern "C"
