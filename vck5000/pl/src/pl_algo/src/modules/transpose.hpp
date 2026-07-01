#ifndef PL_ALGO_TRANSPOSE_HPP
#define PL_ALGO_TRANSPOSE_HPP

// transpose -- Stage 3b: transpose an N x N row-major float matrix in DDR (pure PL,
// no AIE). Two variants for the bandwidth study:
//
//   transpose_naive: read sequential, write STRIDED (out[c*N+r]). The strided write
//     touches one float per DDR beat -> ~1/16 beat utilization (512-bit m_axi = 16
//     floats/beat). Baseline that shows why the transpose would bottleneck the field
//     solve if left unoptimized.
//
//   transpose_tiled: cut the matrix into TILE x TILE tiles; read a tile into an
//     on-chip buffer (contiguous bursts), transpose it in SRAM, write it to the
//     TRANSPOSED tile position (contiguous bursts). Both DDR read and write are
//     contiguous TILE-float runs -> full-beat bursts. The row<->column scatter is
//     confined to the on-chip buffer, where random access is free.
//
// Correctness: a transpose is pure data movement, so both variants must be BIT-EXACT
// vs a host transpose (and each other). Bandwidth (burst inference, efficiency) is
// compared in the C-synth report / hw_emu profiling, not here.

#include "../host_interface.hpp"

namespace plalgo {

constexpr int TILE = 32;   // transpose tile: 32x32 floats = 4 KB on-chip

// Baseline: sequential reads, strided writes (out[c*N+r]).
static void transpose_naive(const float* in, float* out, int N) {
naive_r:
    for (int r = 0; r < N; r++)
    naive_c:
        for (int c = 0; c < N; c++) {
#pragma HLS PIPELINE II=1
            out[c * N + r] = in[r * N + c];   // read contiguous (c), write stride N
        }
}

// Tiled: read tile (tr,tc) -> on-chip transpose -> write tile (tc,tr). Both DDR
// accesses are contiguous TILE-float bursts (inner j is the contiguous dimension).
static void transpose_tiled(const float* in, float* out, int N) {
    const int NT = N / TILE;                  // tiles per side (N % TILE == 0)
    float buf[TILE][TILE];
#pragma HLS bind_storage variable=buf type=RAM_2P

tile_r:
    for (int tr = 0; tr < NT; tr++) {
    tile_c:
        for (int tc = 0; tc < NT; tc++) {
            // read input tile (tr,tc) -> buf, one contiguous TILE-run per tile row
        rd_i:
            for (int i = 0; i < TILE; i++)
            rd_j:
                for (int j = 0; j < TILE; j++) {
#pragma HLS PIPELINE II=1
                    buf[i][j] = in[(tr * TILE + i) * N + (tc * TILE + j)];
                }
            // write transposed tile to output position (tc,tr): out[..] = buf[j][i]
        wr_i:
            for (int i = 0; i < TILE; i++)
            wr_j:
                for (int j = 0; j < TILE; j++) {
#pragma HLS PIPELINE II=1
                    out[(tc * TILE + i) * N + (tr * TILE + j)] = buf[j][i];
                }
        }
    }
}

} // namespace plalgo

#endif // PL_ALGO_TRANSPOSE_HPP
