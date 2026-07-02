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
// TWIDDLE ROM: the DCT-post factor e^{-i*pi*k/2N} is a function of k only, so cos/sin are
// built once into a 1024-entry ROM (twid_cos/twid_sin) and looked up -- replacing the live
// cosf/sinf that, after lane parallelism, was instantiated 8x in the recv datapath. Same
// idea as the HPWL exp LUT. Reusable by dct_1d/dct_row_pass (single-lane, not yet ported).
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
// apply twiddle+Re -> two DCT values, and write them straight into the (float) band at k0,
// k1. The twiddle (c,s) for k0/k1 is looked up ONCE per beat by the caller (same k for all
// 8 lanes) and passed in, so this datapath is pure mul-add -- no live trig. Writes are
// addressed RAM writes (band[bank][k]); no 512-bit-word bit-field packing, whose runtime
// bit-offset barrel-shift logic was the LUT hog.
static inline void dct_recv_lane(hls::stream<axis_t>& from, int b,
                                 float c0, float s0, float c1, float s1,
                                 float band[TILE][GRID], int rib_lane) {
    ap_int<128> d = from.read().data;
    const float re0 = dct_b2f(d.range(31, 0)),  im0 = dct_b2f(d.range(63, 32));
    const float re1 = dct_b2f(d.range(95, 64)), im1 = dct_b2f(d.range(127, 96));
    // DCT_k = Re{ FFT_k * e^{-i*pi*k/2N} } = re*cos + im*sin; twiddle from the shared ROM.
    band[rib_lane][2 * b]     = re0 * c0 + im0 * s0;   // DCT_k0
    band[rib_lane][2 * b + 1] = re1 * c1 + im1 * s1;   // DCT_k1
}

// LOAD task (fill's stage 1): 8 contiguous rows -> row_BRAM as one 512-bit-beat burst
// (16 floats/cycle). row_BRAM is the ping-pong channel to send/recv, so load(batch b+1)
// overlaps send/recv(batch b).
static void dct_load_rows(const float* mat_in, int tr, int batch,
                          float row_BRAM[FFT_LANES][FFT_PTS]) {
    const int r0row = tr * TILE + batch * FFT_LANES;   // first of 8 contiguous rows
    const beat512_t* in512 = reinterpret_cast<const beat512_t*>(mat_in);
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
}

// SEND+RECV task (fill's stage 2): shuffle+stream the 8 rows to the AIE FFT (send), then
// read them back, twiddle+Re, pack into band[rib+*] (recv). send precedes recv within a
// batch (the window FFT needs the whole frame); overlapping send with recv is step 2b.
static void dct_sendrecv_rows(const float row_BRAM[FFT_LANES][FFT_PTS],
                              const float twid_cos[FFT_PTS], const float twid_sin[FFT_PTS],
                              float band[TILE][GRID], int batch,
                              hls::stream<axis_t>& to0, hls::stream<axis_t>& to1,
                              hls::stream<axis_t>& to2, hls::stream<axis_t>& to3,
                              hls::stream<axis_t>& to4, hls::stream<axis_t>& to5,
                              hls::stream<axis_t>& to6, hls::stream<axis_t>& to7,
                              hls::stream<axis_t>& from0, hls::stream<axis_t>& from1,
                              hls::stream<axis_t>& from2, hls::stream<axis_t>& from3,
                              hls::stream<axis_t>& from4, hls::stream<axis_t>& from5,
                              hls::stream<axis_t>& from6, hls::stream<axis_t>& from7) {
    const int N     = GRID;
    const int BEATS = N / CFLOAT_PER_BEAT;   // 512 beats/row
    const int rib   = batch * FFT_LANES;
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
recv:
    for (int b = 0; b < BEATS; b++) {
#pragma HLS PIPELINE II=1
        const int   k0 = 2 * b, k1 = 2 * b + 1;
        const float c0 = twid_cos[k0], s0 = twid_sin[k0];
        const float c1 = twid_cos[k1], s1 = twid_sin[k1];
        dct_recv_lane(from0, b, c0, s0, c1, s1, band, rib + 0);
        dct_recv_lane(from1, b, c0, s0, c1, s1, band, rib + 1);
        dct_recv_lane(from2, b, c0, s0, c1, s1, band, rib + 2);
        dct_recv_lane(from3, b, c0, s0, c1, s1, band, rib + 3);
        dct_recv_lane(from4, b, c0, s0, c1, s1, band, rib + 4);
        dct_recv_lane(from5, b, c0, s0, c1, s1, band, rib + 5);
        dct_recv_lane(from6, b, c0, s0, c1, s1, band, rib + 6);
        dct_recv_lane(from7, b, c0, s0, c1, s1, band, rib + 7);
    }
}

// FILL task (Approach C producer): DCT the band's TILE rows into `band`. Step 2a: nested
// DATAFLOW pipelines load ∥ send/recv across the TILE/FFT_LANES batches (row_BRAM is the
// per-iteration ping-pong channel). `band` is fill's output channel to the outer dataflow.
static void dct_fill_band(const float* mat_in, int tr,
                          const float twid_cos[FFT_PTS], const float twid_sin[FFT_PTS],
                          float band[TILE][GRID],
                          hls::stream<axis_t>& to0, hls::stream<axis_t>& to1,
                          hls::stream<axis_t>& to2, hls::stream<axis_t>& to3,
                          hls::stream<axis_t>& to4, hls::stream<axis_t>& to5,
                          hls::stream<axis_t>& to6, hls::stream<axis_t>& to7,
                          hls::stream<axis_t>& from0, hls::stream<axis_t>& from1,
                          hls::stream<axis_t>& from2, hls::stream<axis_t>& from3,
                          hls::stream<axis_t>& from4, hls::stream<axis_t>& from5,
                          hls::stream<axis_t>& from6, hls::stream<axis_t>& from7) {
batch_loop:
    for (int batch = 0; batch < TILE / FFT_LANES; batch++) {   // 32/8 = 4 batches
#pragma HLS DATAFLOW
        float row_BRAM[FFT_LANES][FFT_PTS];     // 8 lane row buffers (ping-pong channel)
#pragma HLS array_partition variable=row_BRAM complete dim=1          // 8 lane-banks
#pragma HLS array_partition variable=row_BRAM cyclic factor=16 dim=2  // sink 16 floats/beat
        dct_load_rows(mat_in, tr, batch, row_BRAM);
        dct_sendrecv_rows(row_BRAM, twid_cos, twid_sin, band, batch,
                          to0, to1, to2, to3, to4, to5, to6, to7,
                          from0, from1, from2, from3, from4, from5, from6, from7);
    }
}

// WRITE-BACK task (Approach C consumer): write the band's transposed column-stripe (out
// row oc, cols tr*TILE.. = band column oc). In the DATAFLOW region this is stage 2; `band`
// is its input channel. Posted strided write, II~2.
static void dct_writeback_band(const float band[TILE][GRID], float* mat_out, int tr) {
    const int N = GRID;
wr_o:
    for (int oc = 0; oc < N; oc++) {
#pragma HLS PIPELINE II=1
    wr_j:
        for (int j = 0; j < TILE; j++)
            mat_out[oc * N + tr * TILE + j] = band[j][oc];   // addressed read, no bit-field extract
    }
}

// Fused pass: DCT every row band-by-band (8 lanes interleaved), each band written
// TRANSPOSED. num_rows = GRID; host runs the AIE graph g.run(GRID / DENSITY_LANES).
// Approach C (step 1): band-level DATAFLOW pipelines consecutive bands -- writeback(N)
// overlaps fill(N+1) via the ping-ponged per-iteration `band`.
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
    const float PI_F      = 3.14159265358979f;

    // Twiddle ROM: cos/sin of a = pi*k/2N, k=0..N-1 (DCT-post factor). Built ONCE, looked
    // up in recv (pure mul-add). cyclic-2 so k0(even)/k1(odd) reads/beat hit different banks
    // -> II=1 recv. Declared before the dataflow region; passed read-only into each fill.
    float twid_cos[FFT_PTS], twid_sin[FFT_PTS];
#pragma HLS array_partition variable=twid_cos cyclic factor=2
#pragma HLS array_partition variable=twid_sin cyclic factor=2
twid_init:
    for (int k = 0; k < N; k++) {
#pragma HLS PIPELINE II=1
        const float a = PI_F * k / (2.0f * N);
        twid_cos[k] = cosf(a);
        twid_sin[k] = sinf(a);
    }

    // band-level DATAFLOW: `band` is a per-iteration local, so HLS ping-pongs it and
    // pipelines consecutive bands -- writeback(N) overlaps fill(N+1). fill_band produces
    // band, writeback_band consumes it (canonical single-producer/single-consumer).
band_loop:
    for (int tr = 0; tr < NUM_BANDS; tr++) {
#pragma HLS DATAFLOW
        float band[TILE][GRID];                    // corner-turn buffer: TILE x N floats = 128 KB
#pragma HLS array_partition variable=band complete dim=1        // TILE row-banks: parallel column read
#pragma HLS array_partition variable=band cyclic factor=2 dim=2 // even/odd k: recv's 2 writes/beat parallel
        dct_fill_band(mat_in, tr, twid_cos, twid_sin, band,
                      to0, to1, to2, to3, to4, to5, to6, to7,
                      from0, from1, from2, from3, from4, from5, from6, from7);
        dct_writeback_band(band, mat_out, tr);
    }
}

} // namespace plalgo

#endif // PL_ALGO_DCT_TRANSPOSE_HPP
