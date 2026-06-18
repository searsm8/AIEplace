#ifndef PL_ALGO_HPWL_MANAGER_HPP
#define PL_ALGO_HPWL_MANAGER_HPP

// HPWL Manager module.
//
// Role (eventual): drive the AIE HPWL gradient graph -- walk the net/pin
// connectivity, gather pin coordinates, pack net groups, stream them to the AIE,
// receive per-pin partials (dW/dx, dW/dy), and scatter-accumulate them into the
// per-node HPWL gradient buffer.
//
// Milestone B (pass-through): the host concocts the AIE input packet in DDR
// already in the kernel's format (control beat + sorted, SIMD-grouped pin coords;
// see host_interface.hpp). This module just bridges DDR<->AIE: stream the packet
// in, write the returned partials back out. The packing/gather/scatter logic
// lands HERE later, replacing the pass-through and taking coords/net_data/gamma
// inputs instead of a pre-built packet.
//
// Inputs:
//   packet_in  : DDR, host-built AIE input packet (beat_t = 4 floats / 128b)
//   in_beats   : number of 128b beats to stream to the AIE
//   out_beats  : number of 128b beats to read back from the AIE
//   hpwl_from_aie : per-pin partials returned by the AIE HPWL graph
// Outputs:
//   grad_out    : DDR, AIE partials written back (raw, in packet order for now)
//   hpwl_to_aie : pin-coordinate packet streamed to the AIE HPWL graph

#include "../formats.hpp"

namespace plalgo {

static void hpwl_manager(const beat_t* packet_in,
                         beat_t* grad_out,
                         int in_beats,
                         int out_beats,
                         hls::stream<axis_t>& hpwl_to_aie,
                         hls::stream<axis_t>& hpwl_from_aie) {
    // Feed the whole packet, then drain the result. Sequential is safe while the
    // packet is small (AIE output fits in the graph's output FIFO); switch to an
    // HLS DATAFLOW feed/drain pair once packets grow past the FIFO depth.
feed:
    for (int i = 0; i < in_beats; i++) {
#pragma HLS PIPELINE II=1
        axis_t x;
        x.data = packet_in[i];
        x.keep_all();
        hpwl_to_aie.write(x);
    }
drain:
    for (int i = 0; i < out_beats; i++) {
#pragma HLS PIPELINE II=1
        axis_t y = hpwl_from_aie.read();
        grad_out[i] = y.data;
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_MANAGER_HPP
