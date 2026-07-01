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
//   2. loop_flatten off on rd_i/wr_i. HLS auto-flattens the perfect rd_i/rd_j nest into
//      one TILE*TILE-trip loop whose address stride is NON-uniform (jumps by N-TILE at
//      each tile-row boundary) -> not burstable. Keeping rd_j standalone leaves a clean
//      stride-1 length-TILE loop that widens to 512 bits and bursts 2 beats/tile row
//      (iteration latency 75 -> 3). HLS auto-partitions buf_BRAM to source/sink the
//      widened beats, so no manual line buffer is needed.
//
// Correctness: a transpose is pure data movement, so both variants must be BIT-EXACT
// vs a host transpose (and each other). Bandwidth (burst inference, efficiency) is
// compared in the C-synth report / hw_emu profiling, not here.

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

// Tiled: read tile (tr,tc) -> on-chip transpose -> write tile (tc,tr). Both DDR
// accesses are contiguous TILE-float bursts; the row<->column scatter is confined to
// the on-chip buffer, where random access is free. loop_flatten off keeps rd_j/wr_j
// standalone stride-1 length-TILE loops (flattening merges them into a non-uniform-
// stride 1024-trip loop that won't burst).
static void transpose_tiled(const float* in, float* out) {
    const int N  = GRID;                      // compile-time dimension -- REQUIRED for
                                              // the burst (see BURST ROOT CAUSE above)
    const int NT = N / TILE;                  // tiles per side (N % TILE == 0)
    float buf_BRAM[TILE][TILE];
#pragma HLS bind_storage variable=buf_BRAM type=RAM_2P impl=BRAM

tile_r:
    for (int tr = 0; tr < NT; tr++) {
    tile_c:
        for (int tc = 0; tc < NT; tc++) {
            // read input tile (tr,tc) -> buf, one contiguous TILE-float burst per row
        rd_i:
            for (int i = 0; i < TILE; i++) {
#pragma HLS loop_flatten off
            rd_j:
                for (int j = 0; j < TILE; j++) {
#pragma HLS PIPELINE II=1
                    buf_BRAM[i][j] = in[(tr * TILE + i) * N + (tc * TILE + j)];
                }
            }
            // write transposed tile to (tc,tr): out[..] = buf[j][i]
        wr_i:
            for (int i = 0; i < TILE; i++) {
#pragma HLS loop_flatten off
            wr_j:
                for (int j = 0; j < TILE; j++) {
#pragma HLS PIPELINE II=1
                    out[(tc * TILE + i) * N + (tr * TILE + j)] = buf_BRAM[j][i];
                }
            }
        }
    }
}

} // namespace plalgo

#endif // PL_ALGO_TRANSPOSE_HPP
