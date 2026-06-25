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
// The 4th field carries the owning net id (it was 16 B alignment padding): the PL
// scatters per-net reductions indexed by `net` without a separate pin->net map.
// net == -1 marks a pin whose net has no gradient (degree <= 1) -> the kernel skips it.
struct PinRecord {
    int32_t node_idx;      // index into node_pos[]
    float   off_x;         // NetPin.offset.x
    float   off_y;         // NetPin.offset.y
    int32_t net;           // owning net id, or -1 if the net has no gradient
};

// ===========================================================================
//  Buffer 5: result  (PL -> host)            float hpwl
// ===========================================================================
// Total half-perimeter wirelength, summed over all nets:
//     hpwl = sum_nets [ (max_x - min_x) + (max_y - min_y) ]
//   over each net's pin positions. Verified against the host golden
//   DataBase::computeTotalWirelength("HPWL").

// ===========================================================================
//  HPWL GRADIENT EXTENSION  (step 2a -- AIE partials offload)
// ===========================================================================
// The AIE HPWL-gradient graph computes, per pin, the weighted-average (WA)
// wirelength partials dW/dx, dW/dy and the host accumulates them per node.
// Reference math: markv1 computeHpwlPartials_CPU() (Partials.cpp). The on-chip
// kernel is a SIMD port of that, with two fixed properties we must match when
// verifying:
//   * gamma is a COMPILE-TIME constant on the AIE (no runtime params on the
//     VCK5000), so AIE_INV_GAMMA below is the single source of truth and the
//     CPU golden is pinned to it.
//   * exp() is the kernel's fast_exp() approximation -> verify with a tolerance
//     (RMS / R^2 / outlier %), never bit-exact.

// ---- AIE packet geometry (PL <-> AIE HPWL graph) ---------------------------
// Ported verbatim from markv1 (Common.h). The AIE processes nets in SIMD
// "groups": NETS_PER_GROUP nets of the SAME degree share one 8-lane vector,
// with x and y interleaved, so one kernel pass yields both x and y partials for
// 4 nets at once (4 nets * {x,y} = 8 lanes = AIE_VEC).
//   AIE handles net degrees [AIE_NET_MIN, AIE_NET_MAX]; degree-1 nets have no
//   gradient and degree>AIE_NET_MAX nets stay on the host/CPU.
constexpr int   AIE_VEC        = 8;     // floats per AIE vector beat (VEC_SIZE)
constexpr int   NETS_PER_GROUP = 4;     // nets packed across the 8 lanes (x,y interleaved)
constexpr int   AIE_NET_MIN    = 2;     // smallest net degree handled on the AIE
constexpr int   AIE_NET_MAX    = 8;     // largest net degree handled on the AIE
constexpr float AIE_INV_GAMMA  = 0.25f; // 1/gamma baked into the AIE kernel (== markv1)

// ---- AIE input packet  (host -> PL mover -> AIE)  flat float buffer ---------
// One packet per net degree D. Layout (all beats are AIE_VEC floats):
//   beat 0  = control: | (float)D | (float)num_groups | 0 | 0 | 0 | 0 | 0 | 0 |
//   then num_groups blocks, each D beats. Block g, term beat t:
//     | n0.c_t | n0.d_t | n1.c_t | n1.d_t | n2.c_t | n2.d_t | n3.c_t | n3.d_t |
//   where (c,d) = (x,y); nk is the k-th net of group g (k in [0,NETS_PER_GROUP)).
//   SORT CONTRACT (load-bearing -- the kernel seeds b/c sums from terms 0,1):
//     * the x lanes of each net are ordered max-x first (t=0), min-x second (t=1);
//     * the y lanes are ordered max-y first, min-y second -- INDEPENDENTLY of x.
//   So the node sitting in a lane differs between the x and y halves; the host
//   keeps the lane->node_idx map for both axes (see Packer aie_lane_map).
//   Nets past the end of a degree bucket are zero-padded in the trailing lanes.

// ---- AIE output packet  (AIE -> PL mover -> host)  flat float buffer --------
// Same geometry as the input packet MINUS the control beat: num_groups blocks of
// D beats, each beat | n0.gc_t | n0.gd_t | ... | with (gc,gd) = (dW/dx, dW/dy)
// in the SAME lane/term order the host sent. The host scatters each partial onto
// its node (via the lane->node_idx map) with += ; only movable nodes are kept.

// ===========================================================================
//  Buffer 6: per-node HPWL gradient  (PL -> host)  coord_t grad[num_movable]
// ===========================================================================
// Accumulated WA-wirelength gradient for each MOVABLE node (indices [0, M)).
// Fixed nodes ([M, N)) participate in their nets' partials but carry no stored
// gradient. Verified against the pinned-gamma CPU golden with a tolerance.

} // namespace plalgo

#endif // PL_ALGO_HOST_INTERFACE_HPP
