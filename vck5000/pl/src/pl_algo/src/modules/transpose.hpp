#ifndef PL_ALGO_TRANSPOSE_HPP
#define PL_ALGO_TRANSPOSE_HPP

// transpose -- Stage 3b: transpose the GRID x GRID row-major float matrix in DDR
// (pure PL, no AIE). Two variants for the bandwidth study:
//
//   transpose_naive: read sequential, write STRIDED (out[c*N+r]). The strided write
//     touches one float per DDR beat -> ~1/16 beat utilization (512-bit m_axi = 16
//     floats/beat). Baseline that shows why the transpose would bottleneck the field
//     solve if left unoptimized.
//
//   transpose_tiled: cut the matrix into TILE x TILE tiles; read a tile into an
//     on-chip buffer (contiguous bursts), transpose it in SRAM, write it to the
//     TRANSPOSED tile position (contiguous bursts). Both DDR read and write are
//     contiguous TILE-float runs -> full-beat 512-bit bursts. The row<->column scatter
//     is confined to the on-chip buffer, where random access is free.
//
// BURST -- two conditions, both required for the tiled variant to burst (each recovers
// ~16x over the strided baseline; verified in the C-synth 214-115 report):
//   1. N is a COMPILE-TIME constant (GRID). N multiplies into the DDR address
//      ((tr*TILE+i)*N + ...); with a runtime N, HLS cannot prove N % 16 == 0, refuses
//      to widen the m_axi port to 512 bits, and drops the burst entirely (each element
//      becomes a separate ~75-cycle AXI read). cf. dct_one_lane (const N = FFT_PTS).
//   2. The per-tile-row read/write is a clean stride-1 length-TILE run so HLS widens it
//      to 512 bits and bursts 2 beats/tile row. (Flattening the tile-row loop into the
//      element loop yields a NON-uniform stride -- jumps by N-TILE at each tile-row
//      boundary -- that won't burst; here we keep the element loop as the burst unit.)
//
// OVERLAP (Option (a)) -- beat utilization alone left the whole-transpose ~54x off the
// bandwidth floor: with the tile-row loop (rd_i/wr_i) UNpipelined, each tile-row's burst
// is issued, then the datapath STALLS ~70 cyc for the DDR read latency, then the next
// row's burst issues -- latency paid 32x per tile (32,768x overall), never overlapped.
// Fix: PIPELINE the tile-row loop (rd_i/wr_i) so the address issue of row i+1 overlaps
// the data return of row i; the element loop (rd_j/wr_j) unrolls into the pipelined body
// and HLS re-infers the 512-bit burst from the 32 contiguous accesses. Sized by
// num_read_outstanding / num_write_outstanding on gmem10/gmem11 (top.cpp), which bound
// how many row bursts the m_axi adapter keeps in flight. The read phase writes a ROW of
// the on-chip buffer and the write phase reads a COLUMN (buf[j][i]) -- opposite access
// patterns -- so buf is array_partition complete on BOTH dims (registers): one partition
// per element lets both the row write and the column read complete in one cycle. With a
// single-dim partition, whichever phase accesses the un-partitioned direction serializes
// into one BRAM port (dim=1 alone gives rd_i II=32).
//
// Correctness: a transpose is pure data movement, so both variants must be BIT-EXACT
// vs a host transpose (and each other). Bandwidth (burst inference, overlap, efficiency)
// is compared in the C-synth report / hw_emu profiling, not here.

#include "../formats.hpp"
#include "../host_interface.hpp"

namespace plalgo {

constexpr int TILE = 32;   // transpose tile: 32x32 floats = 4 KB on-chip

// Baseline: sequential reads, strided writes (out[c*N+r]).
static void transpose_naive(const float* in, float* out) {
    const int N = GRID;   // compile-time dimension (see BURST ROOT CAUSE above)
naive_r:
    for (int r = 0; r < N; r++)
    naive_c:
        for (int c = 0; c < N; c++) {
#pragma HLS PIPELINE II=1
            out[c * N + r] = in[r * N + c];   // read contiguous (c), write stride N
        }
}

// Tiled + overlapped: read tile (tr,tc) -> on-chip transpose -> write tile (tc,tr).
// Both DDR accesses are contiguous TILE-float bursts; the row<->column scatter is
// confined to the on-chip buffer, where random access is free. The tile-row loops
// (rd_i/wr_i) are PIPELINED so successive row bursts overlap (see OVERLAP note above);
// the element loops (rd_j/wr_j) unroll into the pipelined body and HLS re-infers the
// 512-bit burst from the 32 contiguous accesses.
static void transpose_tiled(const float* in, float* out) {
    const int N  = GRID;                      // compile-time dimension -- REQUIRED for
                                              // the burst (see BURST ROOT CAUSE above)
    const int NT = N / TILE;                  // tiles per side (N % TILE == 0)
    // buf partitioned complete on dim 1 (one BRAM per row): the write phase reads a full
    // COLUMN (buf[j][i]) -- one element from each row-BRAM -> 32 parallel reads, so wr_i
    // pipelines at II=2. (rd_i is capped at II=32 by the single gmem10 read-request port,
    // not by buf; full-partitioning buf to registers removes its port conflict but leaves
    // rd_i at II=32 for only ~2.5% less latency at 1024 FF -- not worth it. See OVERLAP.)
    float buf_BRAM[TILE][TILE];
#pragma HLS bind_storage variable=buf_BRAM type=RAM_2P impl=BRAM
#pragma HLS array_partition variable=buf_BRAM complete dim=1

tile_r:
    for (int tr = 0; tr < NT; tr++) {
    tile_c:
        for (int tc = 0; tc < NT; tc++) {
            // read input tile (tr,tc) -> buf; one 512-bit burst per row, tile-row loop
            // pipelined so successive row bursts overlap (outstanding reads).
        rd_i:
            for (int i = 0; i < TILE; i++) {
#pragma HLS PIPELINE
            rd_j:
                for (int j = 0; j < TILE; j++)
                    buf_BRAM[i][j] = in[(tr * TILE + i) * N + (tc * TILE + j)];
            }
            // write transposed tile to (tc,tr): out[..] = buf[j][i] (column read); one
            // 512-bit burst per row, tile-row loop pipelined so writes overlap.
        wr_i:
            for (int i = 0; i < TILE; i++) {
#pragma HLS PIPELINE
            wr_j:
                for (int j = 0; j < TILE; j++)
                    out[(tc * TILE + i) * N + (tr * TILE + j)] = buf_BRAM[j][i];
            }
        }
    }
}

} // namespace plalgo

#endif // PL_ALGO_TRANSPOSE_HPP
