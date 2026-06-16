#ifndef PL_ALGO_HOST_INTERFACE_HPP
#define PL_ALGO_HOST_INTERFACE_HPP

// host_interface.hpp -- formal host->PL transfer contract for the pl_algo
// minimal (v0) bring-up.
//
// v0 milestone: upload the *static* design once, compute *total HPWL* on the PL,
// read one float back. This proves the host->PL transfer path in sw_emu before
// any iteration / density machinery is added. See DATAFLOW.md for the full
// per-iteration design; formats.hpp owns the (future) per-iteration beat layouts.
//
// This header is POD only -- no STL, no pointers, no virtuals -- so it compiles
// unchanged under both g++ (host/src/pl_algo) and Vitis HLS (pl/src/pl_algo).
// It is the single source of truth for the v0 boundary; host packer and PL
// kernel both include it.

#include <cstdint>

namespace plalgo {

// ---- Coordinate type -------------------------------------------------------
// float, in the host's internal placement units (DEF/bookshelf DBU already
// converted to float by the host). No rescaling happens across the boundary --
// PL and host agree on units so the HPWL readback compares directly against the
// host golden value.
struct coord_t { float x; float y; };

// ===========================================================================
//  Buffer 1: design header  (host -> PL, uploaded once)
// ===========================================================================
// Counts known after parsing. The node index space is a single flat
// enumeration with movable nodes FIRST:
//     node_pos[0 .. num_movable-1]            movable cells (fillers excluded in v0)
//     node_pos[num_movable .. num_nodes-1]    fixed nodes (FIXED components + IOPads)
// => a node is fixed iff its index >= num_movable; no per-node flag needed, and
//    the per-iteration position update (later) touches only the [0,num_movable)
//    prefix.
struct DesignHeader {
    int32_t num_movable;   // M
    int32_t num_nodes;     // N = movable + fixed
    int32_t num_nets;
    int32_t num_pins;      // total pin records == net_ptr[num_nets]
};

// ===========================================================================
//  Buffer 2: node positions  (host -> PL)   coord_t node_pos[num_nodes]
// ===========================================================================
//   - movable first, then fixed (see DesignHeader).
//   - fixed nodes carry their committed (static) position; they never move but
//     their coordinates are needed for the HPWL of nets they sit on.

// ===========================================================================
//  Buffer 3: net CSR offsets  (host -> PL)   int32_t net_ptr[num_nets + 1]
// ===========================================================================
//   - prefix offsets into pins[]; net i owns pins[net_ptr[i] .. net_ptr[i+1]).
//   - net_ptr[0] == 0, net_ptr[num_nets] == num_pins.

// ===========================================================================
//  Buffer 4: flattened pins  (host -> PL)    PinRecord pins[num_pins]
// ===========================================================================
// One record per (net, node) connection, net-major (matches CSR above).
// Absolute pin position = node_pos[node_idx] + {off_x, off_y}.
// A plain cell pin has offset (0,0); only macro pins carry nonzero offsets.
// Padded to 16 B so the array is 128-bit ("beat") aligned, per formats.hpp.
struct PinRecord {
    int32_t node_idx;      // index into node_pos[]
    float   off_x;         // NetPin.offset.x
    float   off_y;         // NetPin.offset.y
    int32_t _pad;
};

// ===========================================================================
//  Buffer 5: result  (PL -> host)            float hpwl
// ===========================================================================
// Total half-perimeter wirelength, summed over all nets:
//     hpwl = sum_nets [ (max_x - min_x) + (max_y - min_y) ]
//   over each net's pin positions. Verified against the host golden
//   DataBase::computeTotalWirelength("HPWL").

} // namespace plalgo

#endif // PL_ALGO_HOST_INTERFACE_HPP
