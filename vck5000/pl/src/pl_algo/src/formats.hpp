#ifndef PL_ALGO_FORMATS_HPP
#define PL_ALGO_FORMATS_HPP

// formats.hpp -- formal data-flow format definitions for the pl_algo (PL-centric) design.
//
// This is the single source of truth for the byte/beat layout at every edge of the
// placement iteration. The whole flow is being re-defined for the PL, so the layouts
// here are the contract that top.cpp and every module agree on. See DATAFLOW.md for
// the narrative description of each stage.
//
// All on-/off-chip transport is 128-bit ("beat") aligned:
//   - DDR buffers are arrays of beat_t (ap_int<128>), 4 floats per beat.
//   - PL<->AIE streams are qdma_axis<128,...> (axis_t), 4 floats / 2 cfloats per beat.

#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

namespace plalgo {

// ---- Global sizing ---------------------------------------------------------
// Hardware grid is 1024x1024 (software testing used 64/32; not used here).
// One real matrix  = 1024*1024*4B = 4 MB.
// One cfloat matrix = 1024*1024*8B = 8 MB.
// => bin density, Ex, Ey, and FFT scratch are all DDR-resident, streamed in row tiles.
constexpr int GRID        = 1024;            // bins per row and per column
constexpr int N_BINS      = GRID * GRID;     // 1,048,576 bins total

// AIE FFT pool: number of rows transformed in parallel (the "8 parallel FFT rows").
constexpr int FFT_LANES   = 8;

// Transport word geometry.
constexpr int BEAT_BITS      = 128;
constexpr int FLOATS_PER_BEAT = BEAT_BITS / 32;   // = 4

// ---- Transport types -------------------------------------------------------
typedef qdma_axis<BEAT_BITS, 0, 0, 0> axis_t;     // PL<->AIE AXI-stream beat
typedef ap_int<BEAT_BITS>             beat_t;      // DDR word (4 packed floats)

// ---- Node coordinate buffer (DDR) ------------------------------------------
// Canonical, single-writer buffer (written only by Memory Writer).
// v1 layout: one movable node per beat, {x, y, pad, pad}.
//   beat[node] = | float x | float y | --- | --- |
// SoA-by-record; 2 of 4 lanes used (revisable: pack 2 nodes/beat once stable).
constexpr int COORDS_BEATS_PER_NODE = 1;

// ---- Per-node HPWL gradient buffer (DDR) -----------------------------------
// Written by HPWL Manager (scatter-accumulate of dW/dx, dW/dy), read by Iteration Update.
//   beat[node] = | float gx | float gy | --- | --- |
constexpr int GRAD_BEATS_PER_NODE = 1;

// ---- Nesterov state buffer (DDR) -------------------------------------------
// Owned by Iteration Update. v1 layout: 2 beats per node.
//   beat0 = | ux | uy | vx | vy |        (current u/v positions)
//   beat1 = | prev_gx | prev_gy | --- | --- |
constexpr int STATE_BEATS_PER_NODE = 2;

// ---- Net / pin connectivity buffer (DDR, read-only) ------------------------
// Variable-degree nets packed sequentially. Per net:
//   header beat = | int num_pins | int net_id | --- | --- |
//   then ceil(num_pins / 2) index beats, each = | int node_idx0 | int node_idx1 | ... |
// HPWL Manager walks this, gathers pin coords from the coords buffer, and builds the
// fixed-size packet sent to the AIE HPWL graph (see below).
//
// ---- HPWL packet (PL <-> AIE HPWL graph) -----------------------------------
// To AIE:   stream of pin coordinates for a net group: | x | y | x | y | ... per beat
// From AIE: stream of per-pin partials             : | dW/dx | dW/dy | ... per beat
// (Net grouping / packet size mirrors markv1's prepareNetGroup; finalized with the
//  aie/src/pl_algo HPWL graph variant.)

// ---- Bin density / E-field matrices (DDR) ----------------------------------
// Row-major 1024x1024 real, 4 MB each. Streamed as FLOATS_PER_BEAT bins per beat,
// GRID/FLOATS_PER_BEAT = 256 beats per row.
constexpr int BEATS_PER_ROW = GRID / FLOATS_PER_BEAT;   // = 256

// ---- FFT I/O (Density Manager PL pre/post <-> AIE FFT) ----------------------
// AIE FFT consumes/produces cfloat: real,imag interleaved (per aie system_settings.h).
//   one beat = | re0 | im0 | re1 | im1 |  (2 complex points per beat)
// One 1024-point row = 512 beats. The Density Manager does DCT/IDCT(/IDXST) pre- and
// post-processing in PL (reorder + twiddle ROM); the AIE does only the FFT.
constexpr int CFLOAT_PER_BEAT = FLOATS_PER_BEAT / 2;    // = 2
constexpr int FFT_BEATS_PER_ROW = GRID / CFLOAT_PER_BEAT; // = 512

// ---- Status block (DDR, host readback) -------------------------------------
// Small buffer written by Metrics, read by host policy between iterations.
//   beat[0] = | float hpwl | float overflow | --- | --- |
constexpr int STATUS_BEATS = 1;

// ---- Transform FSM modes (Density Manager) ---------------------------------
enum transform_mode { TF_IDLE = 0, TF_DCT = 1, TF_IDCT = 2, TF_IDXST = 3 };

} // namespace plalgo

#endif // PL_ALGO_FORMATS_HPP
