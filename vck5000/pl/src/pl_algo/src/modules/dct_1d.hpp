#ifndef PL_ALGO_DCT_1D_HPP
#define PL_ALGO_DCT_1D_HPP

// dct_1d -- Stage 2 first AIE bring-up: one 1D DCT per frame via PL shuffle ->
// AIE forward FFT -> PL twiddle+Re. The AIE does ONLY the FFT (DensityFFTGraph);
// this module is the PL pre/post (Makhoul recipe, proven exact in Stage 0's
// model/density_model.cpp). Streams cfloat beats over fft_to_aie/fft_from_aie
// (128-bit AXIS = 2 cfloat/beat, formats.hpp).
//
// dct_stage 0 (DCT_STAGE_FFT): no shuffle/post -> stream x straight in, write the
//   raw complex FFT out. Verifies the AIE graph + streams + build in isolation.
// dct_stage 1 (DCT_STAGE_DCT): shuffle in, twiddle+Re out -> real DCT vs DCT_naive.
//
// Per frame: write all FFT_PTS points (BEATS beats) then read them back. The AIE
// window-API FFT buffers a full frame, so write-then-read does not deadlock; the
// graph runs num_frames iterations (host: g.run(num_frames)).

#include "../formats.hpp"
#include "../host_interface.hpp"
#include <cmath>

namespace plalgo {

// float <-> raw 32 bits (HLS-safe type-pun).
static inline ap_uint<32> dct_f2b(float f) { union { float f; uint32_t u; } c; c.f = f; return c.u; }
static inline float       dct_b2f(ap_uint<32> b) { union { uint32_t u; float f; } c; c.u = b; return c.f; }

// Makhoul shuffle index: stream position i <- row index (evens increasing order, odds decreasing).
static inline int dct_shuf_idx(int i, int N) { return (i < N / 2) ? (2 * i) : (2 * N - 1 - 2 * i); }

static void dct_1d(const float* dct_in,
                   float*       dct_out,
                   int          num_frames,
                   int          dct_stage,
                   hls::stream<axis_t>& fft_to_aie,
                   hls::stream<axis_t>& fft_from_aie) {
    const int   N     = FFT_PTS;            // 1024
    const int   BEATS = N / CFLOAT_PER_BEAT;// 512 beats per frame (2 cfloat/beat)
    const float PI_F  = 3.14159265358979f;

frame_loop:
    for (int f = 0; f < num_frames; f++) {
        float row_BRAM[FFT_PTS];                 // one input frame, on-chip
#pragma HLS bind_storage variable=row_BRAM type=RAM_2P
    load_row:
        for (int i = 0; i < N; i++) {
#pragma HLS PIPELINE II=1
            row_BRAM[i] = dct_in[f * N + i];
        }

        // ---- stream the frame to the AIE FFT (shuffled for DCT, natural for FFT) ----
    send:
        for (int b = 0; b < BEATS; b++) {
#pragma HLS PIPELINE II=1
            const int i0 = 2 * b, i1 = 2 * b + 1;
            float r0, r1;
            if (dct_stage == DCT_STAGE_DCT) {
                r0 = row_BRAM[dct_shuf_idx(i0, N)];   // Makhoul shuffle (DCT)
                r1 = row_BRAM[dct_shuf_idx(i1, N)];
            } else {
                r0 = row_BRAM[i0]; r1 = row_BRAM[i1]; // natural order (raw FFT)
            }
            ap_int<128> d;
            d.range(31, 0)   = dct_f2b(r0);    // re0
            d.range(63, 32)  = dct_f2b(0.0f);  // im0
            d.range(95, 64)  = dct_f2b(r1);    // re1
            d.range(127, 96) = dct_f2b(0.0f);  // im1
            axis_t v; v.data = d; v.keep_all(); fft_to_aie.write(v);
        }

        // ---- read the FFT result back, post-process, write out ----
    recv:
        for (int b = 0; b < BEATS; b++) {
#pragma HLS PIPELINE II=1
            axis_t v = fft_from_aie.read();
            ap_int<128> d = v.data;
            const float re0 = dct_b2f(d.range(31, 0)),  im0 = dct_b2f(d.range(63, 32));
            const float re1 = dct_b2f(d.range(95, 64)), im1 = dct_b2f(d.range(127, 96));
            const int k0 = 2 * b, k1 = 2 * b + 1;

            if (dct_stage == DCT_STAGE_DCT) {
                // DCT_k = Re{ FFT_k * e^{-i*pi*k/2N} } = re*cos(b) + im*sin(b), b=pi*k/2N.
                const float a0 = PI_F * k0 / (2.0f * N);
                const float a1 = PI_F * k1 / (2.0f * N);
                dct_out[f * N + k0] = re0 * cosf(a0) + im0 * sinf(a0);
                dct_out[f * N + k1] = re1 * cosf(a1) + im1 * sinf(a1);
            } else {
                // passthrough: write raw complex FFT {re,im} per point.
                dct_out[2 * (f * N + k0)]     = re0;
                dct_out[2 * (f * N + k0) + 1] = im0;
                dct_out[2 * (f * N + k1)]     = re1;
                dct_out[2 * (f * N + k1) + 1] = im1;
            }
        }
    }
}

} // namespace plalgo

#endif // PL_ALGO_DCT_1D_HPP
