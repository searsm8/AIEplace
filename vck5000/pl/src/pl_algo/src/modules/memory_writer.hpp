#ifndef PL_ALGO_MEMORY_WRITER_HPP
#define PL_ALGO_MEMORY_WRITER_HPP

// memory_writer -- Stage 5c: the SINGLE writer of the canonical node-coordinate buffer.
//
// The host loads the coords buffer once at startup; thereafter each iteration the
// Iteration Update module streams the updated probe positions v_{k+1} here and the
// Memory Writer commits them into the coords buffer that the next iteration's gradient
// pipeline reads. It is the consumer half of a DATAFLOW pair with iteration_update
// (top.cpp wires the stream). Keeping coords single-writer is what lets the whole
// iteration fuse into one kernel later (Stage 5c.5 note); for v1 it is one stage of the
// MODE_ITERATION_UPDATE invocation.
//
// v1 layout: one movable node per coord_t (see host_interface.hpp) = | x | y |.

#include "../formats.hpp"
#include "../host_interface.hpp"

namespace plalgo {

static void memory_writer(coord_t* coords,               // [M] canonical coords (v), gmem9
                          hls::stream<coord_t>& v_in,    // v_{k+1} from iteration_update
                          int num_movable) {
write_back:
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE
        coords[n] = v_in.read();
    }
}

} // namespace plalgo

#endif // PL_ALGO_MEMORY_WRITER_HPP
