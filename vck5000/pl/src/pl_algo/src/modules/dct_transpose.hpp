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
// address pattern, not the values. Verified vs transpose(DCT_naive), rel_rms ~8e-8.
//
// LANE PARALLELISM (Approach A): the TILE rows of a band are processed FFT_LANES at a time.
// The 8 lanes are INTERLEAVED at the beat level -- one pipelined `send` loop writes one
// beat to each of the 8 lane streams per cycle, then one pipelined `recv` loop drains one
// beat from each per cycle -- so the 8 AIE FFT instances fill and compute IN PARALLEL
// (batch cost ~ 1x send + 1x recv, not 8x). The window-API FFT needs a whole frame before
// it emits output, so within a lane `send` precedes `recv`; the overlap is ACROSS lanes.
// The 8 rows of a batch are contiguous in DDR, so LOAD is one beat-rate burst into the 8
// row buffers (16 floats/cycle). load_i was widened to 512-bit beats earlier (was 46% of
// the pass as a scalar float/cycle load).
//
// DEFERRED: (1) Approach C -- task-level pipeline load/send/FFT/recv/write-back across
// batches+bands (double-buffer the row buffers and the band) so the stages overlap; (2)
// transform_mode (IDCT/IDXST) for the Stage 4 inverse passes, which fuse identically.

#include "../formats.hpp"
#include "../host_interface.hpp"
#include "dct_1d.hpp"      // dct_f2b / dct_b2f / dct_shuf_idx (verified Makhoul DCT helpers)
#include "transpose.hpp"   // beat512_t, TILE (corner-turn buffer word type + band height)
#include <cmath>

namespace plalgo {

constexpr int WORDS_PER_ROW = GRID / 16;   // 512-bit words per DCT'd row (16 floats/word) = 64

// One SEND beat for one lane: Makhoul-shuffle two reals from the lane's row buffer, pack as
// a cfloat beat (imag lanes = 0), stream to the lane's AIE FFT.
static inline void dct_send_lane(const float row[FFT_PTS], int b, int N,
                                 hls::stream<axis_t>& to) {
    const int   i0 = 2 * b, i1 = 2 * b + 1;
    const float r0 = row[dct_shuf_idx(i0, N)];
    const float r1 = row[dct_shuf_idx(i1, N)];
    ap_int<128> d;
    d.range(31, 0)   = dct_f2b(r0);   d.range(63, 32)  = dct_f2b(0.0f);
    d.range(95, 64)  = dct_f2b(r1);   d.range(127, 96) = dct_f2b(0.0f);
    axis_t v; v.data = d; v.keep_all(); to.write(v);
}

// One RECV beat for one lane: read the FFT beat (128b AXIS = 2 complex points = 4 floats),
// twiddle+Re -> two DCT values, pack into the lane's word accumulator, and store a full
// 512-bit word to band[rib_lane] every 8 beats (16 values). DCT_k arrives in k-order, so
// no wide-word read-modify-write is needed.
static inline void dct_recv_lane(hls::stream<axis_t>& from, int b, int N, float PI_F,
                                 beat512_t& wacc, beat512_t band[TILE][WORDS_PER_ROW],
                                 int rib_lane) {
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
    wacc.range(32 * lane0 + 63, 32 * lane0 + 32) = dct_f2b(d1);
    if ((b & 7) == 7) band[rib_lane][b >> 3] = wacc;
}

// Fused pass: DCT every row band-by-band (8 lanes interleaved), each band written
// TRANSPOSED. num_rows = GRID; host runs the AIE graph g.run(GRID / DENSITY_LANES).
static void dct_transpose_pass(const float* mat_in, float* mat_out,
                               hls::stream<axis_t>& to0, hls::stream<axis_t>& to1,
                               hls::stream<axis_t>& to2, hls::stream<axis_t>& to3,
                               hls::stream<axis_t>& to4, hls::stream<axis_t>& to5,
                               hls::stream<axis_t>& to6, hls::stream<axis_t>& to7,
                               hls::stream<axis_t>& from0, hls::stream<axis_t>& from1,
                               hls::stream<axis_t>& from2, hls::stream<axis_t>& from3,
                               hls::stream<axis_t>& from4, hls::stream<axis_t>& from5,
                               hls::stream<axis_t>& from6, hls::stream<axis_t>& from7) {
    const int   N         = GRID;
    const int   NUM_BANDS = N / TILE;              // 32 bands
    const int   BEATS     = N / CFLOAT_PER_BEAT;   // 512 beats/row
    const float PI_F      = 3.14159265358979f;
    const beat512_t* in512 = reinterpret_cast<const beat512_t*>(mat_in);

    beat512_t band[TILE][WORDS_PER_ROW];           // corner-turn buffer: TILE x N floats = 128 KB
#pragma HLS array_partition variable=band complete dim=1   // TILE row-banks: parallel column read

band_loop:
    for (int tr = 0; tr < NUM_BANDS; tr++) {
    batch_loop:
        for (int batch = 0; batch < TILE / FFT_LANES; batch++) {   // 32/8 = 4 batches
            const int rib   = batch * FFT_LANES;
            const int r0row = tr * TILE + rib;      // first of 8 contiguous rows

            float row_BRAM[FFT_LANES][FFT_PTS];     // 8 lane row buffers
#pragma HLS array_partition variable=row_BRAM complete dim=1          // 8 lane-banks
#pragma HLS array_partition variable=row_BRAM cyclic factor=16 dim=2  // sink 16 floats/beat

            // LOAD: 8 contiguous rows as one 512-bit-beat burst (16 floats/cycle).
        load:
            for (int w = 0; w < FFT_LANES * WORDS_PER_ROW; w++) {
#pragma HLS PIPELINE II=1
                beat512_t bt = in512[(size_t)r0row * WORDS_PER_ROW + w];
                const int lane = w / WORDS_PER_ROW;   // 0..7
                const int cw   = w % WORDS_PER_ROW;
            unpack_u:
                for (int u = 0; u < 16; u++) {
#pragma HLS UNROLL
                    row_BRAM[lane][cw * 16 + u] = dct_b2f(bt.range(32 * u + 31, 32 * u));
                }
            }

            // SEND: all 8 lanes, one beat each per cycle (8 stream writes/cycle).
        send:
            for (int b = 0; b < BEATS; b++) {
#pragma HLS PIPELINE II=1
                dct_send_lane(row_BRAM[0], b, N, to0);
                dct_send_lane(row_BRAM[1], b, N, to1);
                dct_send_lane(row_BRAM[2], b, N, to2);
                dct_send_lane(row_BRAM[3], b, N, to3);
                dct_send_lane(row_BRAM[4], b, N, to4);
                dct_send_lane(row_BRAM[5], b, N, to5);
                dct_send_lane(row_BRAM[6], b, N, to6);
                dct_send_lane(row_BRAM[7], b, N, to7);
            }

            // RECV: all 8 lanes, one beat each per cycle, pack into band[rib+0..7].
            beat512_t w0, w1, w2, w3, w4, w5, w6, w7;
        recv:
            for (int b = 0; b < BEATS; b++) {
#pragma HLS PIPELINE II=1
                dct_recv_lane(from0, b, N, PI_F, w0, band, rib + 0);
                dct_recv_lane(from1, b, N, PI_F, w1, band, rib + 1);
                dct_recv_lane(from2, b, N, PI_F, w2, band, rib + 2);
                dct_recv_lane(from3, b, N, PI_F, w3, band, rib + 3);
                dct_recv_lane(from4, b, N, PI_F, w4, band, rib + 4);
                dct_recv_lane(from5, b, N, PI_F, w5, band, rib + 5);
                dct_recv_lane(from6, b, N, PI_F, w6, band, rib + 6);
                dct_recv_lane(from7, b, N, PI_F, w7, band, rib + 7);
            }
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
