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
//   transpose_band: read a BAND of TILE full rows as ONE contiguous DDR burst, transpose
//     it in an on-chip corner-turn buffer, write the transposed column-stripe out. The
//     read-latency fix (Option (b), see BAND). Supersedes the earlier 32x32
//     transpose_tiled (git 400d5c8), whose reads were AR-port-bound at II=32.
//
// BURST (required for either DDR access to widen to a 512-bit burst): N must be a
// COMPILE-TIME constant (GRID). N multiplies into the DDR address; with a runtime N, HLS
// cannot prove N % 16 == 0, refuses to widen the m_axi port to 512 bits, and drops the
// burst (each element a separate ~75-cycle AXI read). cf. dct_one_lane (const N=FFT_PTS).
//
// BAND (Option (b) -- read-latency fix, supersedes the tiled variant's II=32 read).
// A 32x32 TILE is NOT contiguous in DDR: its 32 tile-rows are N floats apart, so reading
// one tile is 32 separate short bursts = 32 read-address (AR) requests. Reads are
// NON-POSTED (the load result is consumed into the buffer), and HLS could not overlap the
// 32 fragmented AR requests on the bundle's single AR channel -> the tiled read stalled at
// II=32 (~70-cyc DDR latency paid per tile-row, 32768x total). A BAND of TILE FULL rows,
// by contrast, IS contiguous (rows tr*TILE .. tr*TILE+TILE-1 = one unbroken TILE*N run):
// it reads as ONE burst -> latency paid once per band (32x total, 1024x fewer AR requests
// than the tiled variant). The corner-turn buffer stores 512-bit WORDS (16 floats packed,
// band[TILE][N/16]) banked complete on dim 1 (TILE row-banks). The read writes ONE word
// per cycle (one contiguous beat, one AR request -> II=1 at beat rate); reading floats
// individually instead makes HLS emit 16 scalar AR requests that serialize on the single
// AR channel (II=16), and a per-float 2D-banked buffer explodes into distributed RAM. The
// write reads a full TILE-element COLUMN as TILE parallel word-reads (float lane oc%16 of
// word oc/16 from each row-bank). The write is the transposed stripe (out column
// tr*TILE..), still strided per output row, but writes are POSTED so they overlap freely
// (II~2) -- the strided write is the residual bottleneck, not the read. COST: the TILE
// wide-word row-banks are the (b) resource price -- measure BRAM/URAM vs the FFT budget.
//
// Correctness: a transpose is pure data movement, so both variants must be BIT-EXACT vs a
// host transpose (and each other). Bandwidth (burst inference, II, efficiency) is compared
// in the C-synth report / hw_emu profiling, not here.

#include <cstring>
#include <cstdint>
#include "../formats.hpp"
#include "../host_interface.hpp"

namespace plalgo {

constexpr int TILE = 32;   // band height: TILE full rows read as one contiguous burst
typedef ap_uint<512> beat512_t;   // DDR read beat: 16 floats packed (matches widened m_axi)

// Baseline: sequential reads, strided writes (out[c*N+r]).
static void transpose_naive(const float* in, float* out) {
    const int N = GRID;   // compile-time dimension (see BURST above)
naive_r:
    for (int r = 0; r < N; r++)
    naive_c:
        for (int c = 0; c < N; c++) {
#pragma HLS PIPELINE II=1
            out[c * N + r] = in[r * N + c];   // read contiguous (c), write stride N
        }
}

// Band (Option (b)): read TILE full rows as one contiguous burst -> on-chip transpose ->
// write the transposed column-stripe. The contiguous band read collapses the tiled
// variant's 32 discontiguous per-tile-row AR requests into ONE burst per band, fixing the
// AR-port-bound II=32 read stall. See BAND above.
static void transpose_band(const float* in, float* out) {
    const int N   = GRID;                     // compile-time dimension -- REQUIRED (BURST)
    const int WORDS_PER_ROW  = N / 16;       // 512-bit words per row (16 floats/word)
    const int NUM_BANDS  = N / TILE;          // number of bands (N % TILE == 0)
    const beat512_t* in512 = reinterpret_cast<const beat512_t*>(in);   // read as 512-bit beats

    beat512_t band[TILE][WORDS_PER_ROW];                 // corner-turn buffer: TILE x N floats = 128 KB
#pragma HLS array_partition variable=band complete dim=1   // TILE row-banks: parallel column read (write phase)

band_r:
    for (int tr = 0; tr < NUM_BANDS; tr++) {
        // READ band as contiguous 512-bit beats: one beat (16 floats) per cycle -> II=1.
    rd:
        for (int w = 0; w < TILE * WORDS_PER_ROW; w++) {
#pragma HLS PIPELINE II=1
            band[w / WORDS_PER_ROW][w % WORDS_PER_ROW] = in512[tr * TILE * WORDS_PER_ROW + w];
        }
        // WRITE transposed stripe: out row oc, cols tr*TILE.. = band column oc. Column oc
        // = float lane oc%16 of word oc/16 in each row-bank (TILE parallel word reads).
    wr_o:
        for (int oc = 0; oc < N; oc++) {
#pragma HLS PIPELINE II=1
            int wcol = oc / 16;
            int lane = oc % 16;
        wr_j:
            for (int j = 0; j < TILE; j++) {
                uint32_t ubits = band[j][wcol].range(32 * lane + 31, 32 * lane);
                float f;
                std::memcpy(&f, &ubits, sizeof(float));
                out[oc * N + tr * TILE + j] = f;
            }
        }
    }
}

} // namespace plalgo

#endif // PL_ALGO_TRANSPOSE_HPP
