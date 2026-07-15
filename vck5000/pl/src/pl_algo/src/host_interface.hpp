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
//  Buffer 4: flattened pins  (host -> PL)    NodePin pins[num_pins]
// ===========================================================================
// One record per (net, node) connection. Absolute pin position =
// node_pos[node_idx] + {off_x, off_y}. A plain cell pin has offset (0,0); only
// macro pins carry nonzero offsets. The `net` field (was 16 B alignment padding)
// carries the owning net id so the PL needs no separate pin->net map; net == -1
// marks a pin whose net has no gradient (degree <= 1) -> the kernel skips it.
//
// The SAME record type backs two DDR arrays in two different orderings:
//   pins  -- NET-major (CSR order, matches net_ptr): used by passes 1-2 (bbox/sums)
//   npins -- NODE-major (sorted ascending by node_idx): used by pass 3 (the
//            segmented reduction). Only movable, gradient-bearing pins appear in
//            npins. Same data, reshuffled so a node's pins are contiguous.
struct NodePin {
    int32_t node_idx;      // index into node_pos[]
    float   off_x;         // NetPin.offset.x
    float   off_y;         // NetPin.offset.y
    int32_t net;           // owning net id, or -1 if the net has no gradient
};

// Per-net reduction results. Computed on-chip in passes 1-2 (net-tiled), spilled
// to DDR (bb_DDR / sums_DDR, [num_nets]) so pass 3 -- which streams node-major --
// can read any net's reduction. Kernel-internal scratch: the host only allocates
// the DDR buffers (num_nets * sizeof), it neither fills nor reads them.
struct NetBBox { float mxx, mnx, mxy, mny; };                     // bounding box
struct NetSums { float Bpx, Bmx, Cpx, Cmx, Bpy, Bmy, Cpy, Cmy; }; // WA B/C sums

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
// Reference math: sw_only computeHpwlPartials_CPU() (Partials.cpp). The on-chip
// kernel is a SIMD port of that, with two fixed properties we must match when
// verifying:
//   * gamma is a COMPILE-TIME constant on the AIE (no runtime params on the
//     VCK5000), so AIE_INV_GAMMA below is the single source of truth and the
//     CPU golden is pinned to it.
//   * exp() is the kernel's fast_exp() approximation -> verify with a tolerance
//     (RMS / R^2 / outlier %), never bit-exact.

// ---- AIE packet geometry (PL <-> AIE HPWL graph) ---------------------------
// Ported verbatim from sw_only (Common.h). The AIE processes nets in SIMD
// "groups": NETS_PER_GROUP nets of the SAME degree share one 8-lane vector,
// with x and y interleaved, so one kernel pass yields both x and y partials for
// 4 nets at once (4 nets * {x,y} = 8 lanes = AIE_VEC).
//   AIE handles net degrees [AIE_NET_MIN, AIE_NET_MAX]; degree-1 nets have no
//   gradient and degree>AIE_NET_MAX nets stay on the host/CPU.
constexpr int   IGNORE_NET_DEGREE = 100; // XPlace net_mask / ignore_net_degree: nets with more
                                         // pins are excluded from the WA gradient AND reported HPWL
                                         // (clock/reset/scan). == sw_only cfg ignore_net_degree.
constexpr int   AIE_VEC        = 8;     // floats per AIE vector beat (VEC_SIZE)
constexpr int   NETS_PER_GROUP = 4;     // nets packed across the 8 lanes (x,y interleaved)
constexpr int   AIE_NET_MIN    = 2;     // smallest net degree handled on the AIE
constexpr int   AIE_NET_MAX    = 8;     // largest net degree handled on the AIE
constexpr float AIE_INV_GAMMA  = 0.25f; // 1/gamma baked into the AIE kernel (== sw_only)

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

// ===========================================================================
//  DENSITY EXTENSION  (Stage 1 -- bin density on the PL)
// ===========================================================================
// The density_manager scatters node areas into the GRID x GRID bin-density grid
// rho (the ePlace charge density), as step 1 of the electrostatic-field solve.
// Stage 1 implements only the binning + readback; the DCT/FFT field solve follows
// in later stages. Reference: sw_only Grid::computeBinOverlaps + clampFixedDensity
// + getBinDensities (Density.cpp::computeOverlaps). Fillers are EXCLUDED in v1
// (TODO: add fillers once the field pipeline is in place).

// Bin grid dimension, host-visible. Must equal formats.hpp GRID (the PL transport
// header pulls HLS types, so host TUs can't include it -- this is the host copy).
constexpr int DENSITY_GRID  = 1024;
constexpr int DENSITY_NBINS = DENSITY_GRID * DENSITY_GRID;   // 1,048,576 (4 MB float)
constexpr int DENSITY_LANES = 8;   // AIE FFT pool width; must equal formats.hpp FFT_LANES
                                   // (and AIE_DENSITY_INSTANCES at build time)

// Host copy of formats.hpp `transform_mode` (host TUs can't include formats.hpp, which pulls
// HLS types). Passed to MODE_DCT_TRANSPOSE via the dct_stage arg. Distinct names from the
// formats.hpp TF_* enum because top.cpp includes BOTH headers. Values MUST stay in sync.
enum transform_mode_host { TFH_DCT = 1, TFH_IDCT = 2, TFH_IDXST = 3 };

// ---- top() mode selector ---------------------------------------------------
// One PL kernel serves multiple modules during bring-up; `mode` selects which.
// (Stage 5 replaces this with the unified per-iteration datapath.)
enum top_mode { MODE_HPWL_GRAD = 0, MODE_DENSITY_BIN = 1, MODE_DCT_1D = 2,
                MODE_DCT_ROWPASS = 3,    // Stage 3a: DCT all rows via the 8-lane pool
                MODE_TRANSPOSE = 4,      // Stage 3b: N x N matrix transpose (pure PL).
                                         // dct_stage selects variant: 0=naive, 1=tiled;
                                         // num_frames = N (matrix side); dct_in->dct_out.
                MODE_DCT_TRANSPOSE = 5,  // Stage 3c/4: FUSED transform row-pass + transpose
                                         // (one pass = transform all rows, written
                                         // transposed). dct_stage carries the transform_mode
                                         // (TF_DCT/TF_IDCT/TF_IDXST); num_frames = N;
                                         // dct_in -> dct_out (transposed).
                MODE_SPECTRAL = 6,       // Stage 4: spectral multiply a_uv -> one field
                                         // (dct_stage = axis: 0 = Ex/w_u, 1 = Ey/w_v).
                                         // num_frames = N; dct_in (a_uv) -> dct_out (field).
                MODE_FORCE_GATHER = 7,   // Stage 5: per-node density gradient = gather
                                         // sum_bins overlap_area * eField. Reuses ports:
                                         // eField_x = bin_density (gmem9), eField_y = dct_in
                                         // (gmem10), node_box (gmem8) -> node_grad (gmem7).
                                         // scalars: num_movable, bin_w, bin_h.
                MODE_ITERATION_UPDATE = 8, // Stage 5c: one Nesterov step (combine + precond +
                                         // BB step + momentum + die clamp). DATAFLOW pair
                                         // iteration_update -> memory_writer. Reuses ports:
                                         // u_k = node_pos (gmem0), precond = exp_lut (gmem4),
                                         // g_hpwl = node_grad (gmem7), {v_k,size} = node_box
                                         // (gmem8), g_density = dct_in (gmem10); OUT: u_{k+1}
                                         // = dct_out (gmem11), v_{k+1} = bin_density (gmem9).
                                         // scalars: lambda=inv_gamma, alpha=inv_lut_step,
                                         // coeff=bin_w, die_xmax=bin_h, die_ymax=target_density,
                                         // num_movable.
                MODE_METRICS = 9 };      // Stage 5c: reduce {HPWL, overflow_sum} for the host
                                         // policy. HPWL from node_pos (gmem0) + net_ptr (gmem1)
                                         // + pins (gmem2); overflow_sum from bin_density (gmem9).
                                         // OUT: dct_out[0]=HPWL, dct_out[1]=overflow_sum (gmem11).
                                         // scalars: num_nets, target_density.

// ---- 1D DCT via the AIE FFT  (Stage 2 -- first AIE bring-up) ----------------
// MODE_DCT_1D streams num_frames real rows of FFT_PTS points each through:
// PL shuffle -> AIE forward FFT (fft_to_aie/fft_from_aie streams) -> PL twiddle+Re.
// One forward FFT serves DCT/IDCT/IDXST (PL does all pre/post); Stage 0's model
// proved the recipe exact. FFT_PTS == DENSITY_GRID (a grid row is one 1024-pt FFT).
//   dct_in  : float[num_frames * FFT_PTS]        real input rows
//   dct_out : stage 0 -> float[num_frames*FFT_PTS*2] complex FFT {re,im} per point
//             stage 1 -> float[num_frames*FFT_PTS]   real DCT per point
// dct_stage de-risks the bring-up in two steps (see dct_stage_t):
//   0 (FFT passthrough): no shuffle/post -> isolates AIE graph + streams + build.
//   1 (full DCT):        PL shuffle + twiddle/Re -> vs DCT_naive.
constexpr int FFT_PTS = DENSITY_GRID;   // 1024-pt FFT (one grid row)
enum dct_stage_t { DCT_STAGE_FFT = 0, DCT_STAGE_DCT = 1 };

// ---- Per-node geometry  (host -> PL)   NodeBox node_box[num_nodes] ----------
// Lower-left anchor (x,y) (== node_pos) + cell size (w,h). Movable [0,M), fixed
// [M,N) -- same index split as node_pos, so status is implicit. Separate buffer
// from node_pos so the verified HPWL contract is untouched.
struct NodeBox { float x; float y; float w; float h; };

// ---- Bin density rho  (PL -> host for Stage 1; DDR scratch later) -----------
//   float bin_density[GRID*GRID], row-major FIRST-INDEX(x)-major:
//     bin_density[x*GRID + y]   x horizontal in [0,GRID), y vertical in [0,GRID)
//   to match sw_only density[x][y] and make a fixed-x "row" contiguous (the DCT's
//   row direction). Natural float (128-bit beat packing deferred, like hpwl_CU).
//   bin_w = die_xsize/GRID, bin_h = die_ysize/GRID; bin indexing assumes die
//   origin (0,0) (sw_only convention). rho = clamped_overlap / (bin_w*bin_h);
//   fixed overlap is clamped to target_density*bin_area before movable is added.

} // namespace plalgo

#endif // PL_ALGO_HOST_INTERFACE_HPP
