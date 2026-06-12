#ifndef PL_ALGO_MEMORY_WRITER_HPP
#define PL_ALGO_MEMORY_WRITER_HPP

// Memory Writer module (black box for v1).
//
// Role: the SINGLE writer of the canonical node-coordinate buffer in DDR. The host
// loads it once at startup; thereafter, each iteration the Iteration Update module
// streams updated coordinates here and the Memory Writer commits them back into the
// SAME DDR region (replaces the old host-side I/O module).
//
// Inputs:
//   coords_in : stream of updated node coordinates from Iteration Update,
//               one beat per node = | x | y | pad | pad |  (see formats.hpp)
//   num_nodes : number of movable nodes
// Output:
//   coords    : canonical DDR coordinate buffer (overwritten in place)

#include "../formats.hpp"

namespace plalgo {

static void memory_writer(beat_t* coords,
                          hls::stream<axis_t>& coords_in,
                          int num_nodes) {
    // TODO: commit streamed coordinates to the canonical DDR buffer.
    // Stub: drain the stream and write through so HLS keeps the ports live.
write_back:
    for (int n = 0; n < num_nodes; n++) {
        axis_t beat = coords_in.read();
        coords[n] = beat.data;
    }
}

} // namespace plalgo

#endif // PL_ALGO_MEMORY_WRITER_HPP
