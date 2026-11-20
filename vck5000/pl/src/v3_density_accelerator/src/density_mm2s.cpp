#include "include.h"
#include "twiddle_table.h"
#include "omega_table.h"


// PL kernel: read from DDR in 512-bit words and send 64-bit beats on AXIS
void mm2s_dct_tile(const ddr* mem,
    const unsigned int mem_base,
    const unsigned int tile_offset,

    aie_stream& s0,  aie_stream& s1,
    aie_stream& s2,  aie_stream& s3,
    aie_stream& s4,  aie_stream& s5,
    aie_stream& s6,  aie_stream& s7,
    aie_stream& s8,  aie_stream& s9,
    aie_stream& s10, aie_stream& s11,
    aie_stream& s12, aie_stream& s13,
    aie_stream& s14, aie_stream& s15
    ) {

DDR_READ_LOOP:
  for (unsigned int n = 0; n < MATRIX_DIMENSION; ++n) {
#pragma HLS PIPELINE II=1

    // Compute DDR address:
    //   mem[2n]                  for 0 <= n < MATRIX_DIMENSION/2
    //   mem[2*MATRIX_DIMENSION - 1 - 2n]     for MATRIX_DIMENSION/2 <= n < MATRIX_DIMENSION
    unsigned int logical_addr;

    if (n < (MATRIX_DIMENSION >> 1)) {
      logical_addr = 2 * n;
    } else {
      logical_addr = 2 * MATRIX_DIMENSION - 1 - 2 * n;
    }

    const unsigned int addr = mem_base +
      logical_addr * DDR_WORDS_PER_MATRIX_DIMENSION +
      tile_offset;

    ddr wide_word = mem[addr];

    axis_pkt v[ELEMENTS_PER_DDR_WORD];
#pragma HLS ARRAY_PARTITION variable=v complete

    // send the 16 32-bit real parts from the 512-bit mem read
    // to the 16 different channels in a 64-bit packet, where
    // the LSB are the imaginary part set to 0.
    for (unsigned int k = 0; k < ELEMENTS_PER_DDR_WORD; ++k) {
#pragma HLS UNROLL
      v[k].data.range(31, 0)  = wide_word.range(32*k + 31, 32*k);      // Real
      v[k].data.range(63, 32) = 0;                                     // Imaginary = 0.0f
      v[k].keep = -1;    // all 8 bytes valid
      v[k].strb = -1;    // all 8 bytes active
      v[k].last = (n == MATRIX_DIMENSION - 1);
    }

    // Send each slice to its corresponding stream
    s0.write(v[0]);
    s1.write(v[1]);
    s2.write(v[2]);
    s3.write(v[3]);
    s4.write(v[4]);
    s5.write(v[5]);
    s6.write(v[6]);
    s7.write(v[7]);
    s8.write(v[8]);
    s9.write(v[9]);
    s10.write(v[10]);
    s11.write(v[11]);
    s12.write(v[12]);
    s13.write(v[13]);
    s14.write(v[14]);
    s15.write(v[15]);
  }
}

static bool is_idxst_mode(const TransformMode mode)
{
#pragma HLS INLINE

    return mode == TransformMode::IDXST ||
           mode == TransformMode::IDXST_FIELD_PREP;
}

static float apply_field_preprocessing(
    const float coefficient,
    const unsigned int u,
    const unsigned int v,
    const TransformMode mode)
{
#pragma HLS INLINE

    if (mode != TransformMode::IDCT_FIELD_PREP &&
        mode != TransformMode::IDXST_FIELD_PREP) {
        return coefficient;
    }

    if (u == 0 && v == 0) {
        return 0.0f;
    }

    const float denominator =
        omega_squared_table[u] +
        omega_squared_table[v];

    const float numerator =
        (mode == TransformMode::IDCT_FIELD_PREP)
            ? omega_table[u]
            : omega_table[v];

    return coefficient * numerator / denominator;
}

void mm2s_idct_tile(
    const ddr* mem,
    const unsigned int mem_base,
    const unsigned int tile_offset,
    const TransformMode mode,

    aie_stream& s0,  aie_stream& s1,
    aie_stream& s2,  aie_stream& s3,
    aie_stream& s4,  aie_stream& s5,
    aie_stream& s6,  aie_stream& s7,
    aie_stream& s8,  aie_stream& s9,
    aie_stream& s10, aie_stream& s11,
    aie_stream& s12, aie_stream& s13,
    aie_stream& s14, aie_stream& s15)
{
    const bool idxst = is_idxst_mode(mode);

IDCT_DDR_READ_LOOP:
    for (unsigned int n = 0; n < MATRIX_DIMENSION; ++n) {
#pragma HLS PIPELINE II=2

        /*
         * Read the original coefficients:
         *
         *     x[n]
         *     x[N-n], for n != 0
         */
        const unsigned int addr_n =
            mem_base +
            n * DDR_WORDS_PER_MATRIX_DIMENSION +
            tile_offset;

        const ddr word_n = mem[addr_n];

        unsigned int mirror_n = 0;
        ddr word_mirror = 0;

        if (n != 0) {
            mirror_n = MATRIX_DIMENSION - n;

            const unsigned int addr_mirror =
                mem_base +
                mirror_n * DDR_WORDS_PER_MATRIX_DIMENSION +
                tile_offset;

            word_mirror = mem[addr_mirror];
        }

        /*
         * twiddle_re[n] =  cos(theta_n)
         * twiddle_im[n] = -sin(theta_n)
         */
        const float tw_re = twiddle_re[n];
        const float tw_im = twiddle_im[n];

        axis_pkt packet[ELEMENTS_PER_DDR_WORD];
#pragma HLS ARRAY_PARTITION variable=packet complete

IDCT_PREPROCESS_LOOP:
        for (unsigned int lane = 0;
             lane < ELEMENTS_PER_DDR_WORD;
             ++lane) {
#pragma HLS UNROLL

            const unsigned int u =
                tile_offset * ELEMENTS_PER_DDR_WORD + lane;

            const ap_uint<32> original_n_bits =
                word_n.range(32 * lane + 31, 32 * lane);

            float original_n =
                bits_to_float(original_n_bits);

            /*
             * Apply field preparation using the original frequency
             * coordinate n, before the IDXST reversal.
             */
            original_n = apply_field_preprocessing(
                original_n, u, n, mode);

            float original_mirror = 0.0f;

            if (n != 0) {
                const ap_uint<32> original_mirror_bits =
                    word_mirror.range(
                        32 * lane + 31,
                        32 * lane);

                original_mirror =
                    bits_to_float(original_mirror_bits);

                /*
                 * word_mirror contains the original coefficient
                 * x[N-n], whose frequency coordinate is mirror_n.
                 */
                original_mirror = apply_field_preprocessing(
                    original_mirror, u, mirror_n, mode);
            }

            /*
             * Construct the sequence q to which IDCT is applied.
             *
             * IDCT:
             *     q[n]   = x[n]
             *     q[N-n] = x[N-n]
             *
             * IDXST:
             *     q[n]   = x[N-n]
             *     q[N-n] = x[n]
             *
             * For IDXST and n==0:
             *
             *     q[0] = x[N] = 0
             */
            float q_n;
            float q_mirror;

            if (idxst) {
                if (n == 0) {
                    q_n      = 0.0f;
                    q_mirror = 0.0f;
                } else {
                    q_n      = original_mirror;
                    q_mirror = original_n;
                }
            } else {
                q_n      = original_n;
                q_mirror = original_mirror;
            }

            /*
             * Reconstruct conj(Y[n]) for the forward-FFT-based
             * inverse FFT:
             *
             *   conj(Y[n])
             *       = exp(-j*theta_n)
             *         * (q[n] + j*q[N-n])
             *
             * Since tw_im = -sin(theta):
             *
             *   real = tw_re*q[n] - tw_im*q[N-n]
             *   imag = tw_re*q[N-n] + tw_im*q[n]
             */
            float fft_input_real;
            float fft_input_imag;

            if (n == 0) {
                fft_input_real = q_n;
                fft_input_imag = 0.0f;
            } else {
                fft_input_real =
                    tw_re * q_n -
                    tw_im * q_mirror;

                fft_input_imag =
                    tw_re * q_mirror +
                    tw_im * q_n;
            }

            packet[lane].data.range(31, 0) =
                float_to_bits(fft_input_real);

            packet[lane].data.range(63, 32) =
                float_to_bits(fft_input_imag);

            packet[lane].keep = -1;
            packet[lane].strb = -1;
            packet[lane].last =
                (n == MATRIX_DIMENSION - 1);
        }

        s0.write(packet[0]);
        s1.write(packet[1]);
        s2.write(packet[2]);
        s3.write(packet[3]);
        s4.write(packet[4]);
        s5.write(packet[5]);
        s6.write(packet[6]);
        s7.write(packet[7]);
        s8.write(packet[8]);
        s9.write(packet[9]);
        s10.write(packet[10]);
        s11.write(packet[11]);
        s12.write(packet[12]);
        s13.write(packet[13]);
        s14.write(packet[14]);
        s15.write(packet[15]);
    }
}

void density_mm2s(const ddr* mem,
    const unsigned int mem_base,
    const TransformMode mode,

    // Streams from PL to AIE
    aie_stream& s0,  aie_stream& s1,
    aie_stream& s2,  aie_stream& s3,
    aie_stream& s4,  aie_stream& s5,
    aie_stream& s6,  aie_stream& s7,
    aie_stream& s8,  aie_stream& s9,
    aie_stream& s10, aie_stream& s11,
    aie_stream& s12, aie_stream& s13,
    aie_stream& s14, aie_stream& s15
    ) {

  switch(mode) {
    case TransformMode::DCT:
    case TransformMode::DCT_REMOVE_DC:
      for (unsigned int tile_offset = 0; tile_offset < DDR_WORDS_PER_MATRIX_DIMENSION; tile_offset++ ) {
        mm2s_dct_tile(mem, mem_base, tile_offset,
            s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15);
      }
      break;
    case TransformMode::IDCT:
    case TransformMode::IDCT_FIELD_PREP:
    case TransformMode::IDXST:
    case TransformMode::IDXST_FIELD_PREP:
      for (unsigned int tile_offset = 0; tile_offset < DDR_WORDS_PER_MATRIX_DIMENSION; tile_offset++) {
        mm2s_idct_tile(mem, mem_base, tile_offset, mode,
            s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15);
      }
      break;
  }

}

