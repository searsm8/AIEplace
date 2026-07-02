#ifndef PL_ALGO_DCT_TRANSPOSE_HPP
#define PL_ALGO_DCT_TRANSPOSE_HPP

// dct_transpose -- Stage 3c: FUSED 1D-DCT-row-pass + transpose ("streaming transpose").
//
// One pass = DCT every row via the AIE FFT pool, but instead of writing the DCT'd rows
// back to DDR contiguously and RE-READING them for a separate transpose, it accumulates a
// BAND of TILE DCT'd rows in an on-chip corner-turn buffer, then writes the band out
// TRANSPOSED (the beat-rate stripe write proven in transpose_band). Two of these passes =
// the forward 2D DCT (a_uv), which halves the DDR traffic of the unfused
// DCT/transpose/DCT/transpose flow (8 matrix passes -> 4): the intermediate write+read of
// each DCT'd matrix never touches DDR.
//
// Correctness is unchanged from the verified pieces: the DCT math is dct_one_lane's
// (Makhoul shuffle -> forward FFT -> twiddle+Re, proven exact in Stage 0/2), and the
// transpose is pure data movement, so folding it into the write only changes the OUTPUT
// address pattern, not the values. Verify one pass vs a host row-DCT-then-transpose
// reference (float tol, DCT carries rounding -- NOT bit-exact), then two passes vs
// compute_a_uv_DCT.
//
// Structure per band (TILE rows): FILL the band buffer by DCT'ing TILE rows through the
// FFT_LANES-wide pool (TILE/FFT_LANES lane-batches, reusing dct_one_lane's path but
// redirecting its post into band[rib]), THEN write the band's transposed column-stripe.
// Fill and write-back are sequential -> no concurrent RMW on the buffer (overlap via a
// double buffer is a later throughput opt, gated on the write-back not already hiding
// under the FFT).
//
// LATENCY: load_i reads mat_in as 512-bit beats (16 floats/cycle) into a cyclic-banked row
// buffer -- one AR request per beat, beat-rate input (it was 46% of the pass as a scalar
// float/cycle load). DEFERRED (measured next): (1) overlap the FFT_LANES lanes (send/recv
// run sequentially now, so the 8-lane pool does not parallelize); (2) double-buffer the
// band to overlap FFT fill with the transposed write-back; (3) transform_mode (IDCT/IDXST)
// for the Stage 4 inverse passes, which fuse identically.

#include "../formats.hpp"
#include "../host_interface.hpp"
#include "dct_1d.hpp"      // dct_f2b / dct_b2f / dct_shuf_idx (verified Makhoul DCT helpers)
#include "transpose.hpp"   // beat512_t, TILE (corner-turn buffer word type + band height)
#include <cmath>

namespace plalgo {

constexpr int WORDS_PER_ROW = GRID / 16;   // 512-bit words per DCT'd row (16 floats/word) = 64

// DCT one row (mat_in[row_global]) into band[rib], packed as 512-bit words. SAME path as
// dct_one_lane (shuffle pre -> AIE forward FFT -> twiddle+Re post); only the post's
// destination changes -- band buffer instead of DDR. DCT_k arrives in k-order, so 8 recv
// beats (16 values) complete one word: accumulate in wacc, store once (no wide-word RMW).
static void dct_lane_to_band(const float* mat_in, beat512_t band[TILE][WORDS_PER_ROW],
                             int row_global, int rib,
                             hls::stream<axis_t>& to, hls::stream<axis_t>& from) {
    const int   N     = FFT_PTS;              // 1024
    const int   BEATS = N / CFLOAT_PER_BEAT;  // 512 beats/row (2 cfloat/beat)
    const float PI_F  = 3.14159265358979f;

    const beat512_t* in512 = reinterpret_cast<const beat512_t*>(mat_in);   // read as beats
    float row_BRAM[FFT_PTS];
#pragma HLS bind_storage variable=row_BRAM type=RAM_2P
#pragma HLS array_partition variable=row_BRAM cyclic factor=16   // sink one 512-bit beat/cycle
load_i:
    for (int w = 0; w < WORDS_PER_ROW; w++) {   // WORDS_PER_ROW beats/row: one 512-bit load/cycle
#pragma HLS PIPELINE II=1
        beat512_t bt = in512[row_global * WORDS_PER_ROW + w];
    unpack_u:
        for (int u = 0; u < 16; u++) {
#pragma HLS UNROLL
            row_BRAM[w * 16 + u] = dct_b2f(bt.range(32 * u + 31, 32 * u));   // 16 floats -> 16 banks
        }
    }
send_b:
    for (int b = 0; b < BEATS; b++) {          // Makhoul shuffle + stream to the lane
#pragma HLS PIPELINE II=1
        const int i0 = 2 * b, i1 = 2 * b + 1;
        const float r0 = row_BRAM[dct_shuf_idx(i0, N)];
        const float r1 = row_BRAM[dct_shuf_idx(i1, N)];
        ap_int<128> d;
        d.range(31, 0)   = dct_f2b(r0);   d.range(63, 32)  = dct_f2b(0.0f);
        d.range(95, 64)  = dct_f2b(r1);   d.range(127, 96) = dct_f2b(0.0f);
        axis_t v; v.data = d; v.keep_all(); to.write(v);
    }
    beat512_t wacc;                            // one 512-bit word under construction
recv_b:
    for (int b = 0; b < BEATS; b++) {          // read FFT, twiddle+Re, PACK into band[rib]
#pragma HLS PIPELINE II=1
        ap_int<128> d = from.read().data;
        const float re0 = dct_b2f(d.range(31, 0)),  im0 = dct_b2f(d.range(63, 32));
        const float re1 = dct_b2f(d.range(95, 64)), im1 = dct_b2f(d.range(127, 96));
        const int k0 = 2 * b, k1 = 2 * b + 1;
        // DCT_k = Re{ FFT_k * e^{-i*pi*k/2N} } = re*cos(a) + im*sin(a), a = pi*k/2N
        const float a0 = PI_F * k0 / (2.0f * N), a1 = PI_F * k1 / (2.0f * N);
        const float d0 = re0 * cosf(a0) + im0 * sinf(a0);   // DCT_k0
        const float d1 = re1 * cosf(a1) + im1 * sinf(a1);   // DCT_k1
        const int lane0 = k0 & 15;                          // slot in the 512-bit word (k0 even)
        wacc.range(32 * lane0 + 31, 32 * lane0)      = dct_f2b(d0);
        wacc.range(32 * lane0 + 63, 32 * lane0 + 32) = dct_f2b(d1);   // lane0+1
        if ((b & 7) == 7)                                   // 8 beats = 16 values = 1 word
            band[rib][b >> 3] = wacc;                       // word index k/16 = b/8
    }
}

// Fused pass: DCT every row band-by-band, each band written TRANSPOSED. num_rows = GRID
// (full matrix); host runs the AIE graph g.run(num_rows). Streams are the 8 named lanes
// (HLS has no top-level AXIS stream arrays; cf. dct_row_pass).
static void dct_transpose_pass(const float* mat_in, float* mat_out,
                               hls::stream<axis_t>& to0, hls::stream<axis_t>& to1,
                               hls::stream<axis_t>& to2, hls::stream<axis_t>& to3,
                               hls::stream<axis_t>& to4, hls::stream<axis_t>& to5,
                               hls::stream<axis_t>& to6, hls::stream<axis_t>& to7,
                               hls::stream<axis_t>& from0, hls::stream<axis_t>& from1,
                               hls::stream<axis_t>& from2, hls::stream<axis_t>& from3,
                               hls::stream<axis_t>& from4, hls::stream<axis_t>& from5,
                               hls::stream<axis_t>& from6, hls::stream<axis_t>& from7) {
    const int N         = GRID;
    const int NUM_BANDS  = N / TILE;           // 32 bands

    beat512_t band[TILE][WORDS_PER_ROW];       // corner-turn buffer: TILE x N floats = 128 KB
#pragma HLS array_partition variable=band complete dim=1   // TILE row-banks: parallel column read

band_loop:
    for (int tr = 0; tr < NUM_BANDS; tr++) {
        // FILL: DCT this band's TILE rows into the buffer (TILE/FFT_LANES lane-batches).
    fill:
        for (int batch = 0; batch < TILE / FFT_LANES; batch++) {   // 32/8 = 4 batches
            const int rib = batch * FFT_LANES;
            const int r   = tr * TILE + rib;
            dct_lane_to_band(mat_in, band, r + 0, rib + 0, to0, from0);
            dct_lane_to_band(mat_in, band, r + 1, rib + 1, to1, from1);
            dct_lane_to_band(mat_in, band, r + 2, rib + 2, to2, from2);
            dct_lane_to_band(mat_in, band, r + 3, rib + 3, to3, from3);
            dct_lane_to_band(mat_in, band, r + 4, rib + 4, to4, from4);
            dct_lane_to_band(mat_in, band, r + 5, rib + 5, to5, from5);
            dct_lane_to_band(mat_in, band, r + 6, rib + 6, to6, from6);
            dct_lane_to_band(mat_in, band, r + 7, rib + 7, to7, from7);
        }
        // WRITE-BACK: transposed stripe (out row oc, cols tr*TILE.. = band column oc) --
        // the transpose_band write. Column oc = float lane oc%16 of word oc/16 in each
        // row-bank (TILE parallel word reads). Posted strided write, II~2.
    wr_o:
        for (int oc = 0; oc < N; oc++) {
#pragma HLS PIPELINE II=1
            const int wcol = oc / 16, lane = oc % 16;
        wr_j:
            for (int j = 0; j < TILE; j++) {
                uint32_t ubits = band[j][wcol].range(32 * lane + 31, 32 * lane);
                float f;
                std::memcpy(&f, &ubits, sizeof(float));
                mat_out[oc * N + tr * TILE + j] = f;
            }
        }
    }
}

} // namespace plalgo

#endif // PL_ALGO_DCT_TRANSPOSE_HPP
