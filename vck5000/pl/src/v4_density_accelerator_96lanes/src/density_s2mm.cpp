#include "include.h"
#include "twiddle_table.h"


// PL kernel: receive 64-bit words from AXIS and write to DDR as 512-bit beats
void s2mm_dct_tile(ddr* mem,
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
    aie_stream& s14, aie_stream& s15
    ) {

  // Each bank contains one 512-bit word for each destination row.
  ddr ping[16];
  ddr pong[16];
#pragma HLS ARRAY_PARTITION variable=ping complete
#pragma HLS ARRAY_PARTITION variable=pong complete

DDR_WRITE_LOOP:

  for (unsigned int n = 0; n < MATRIX_DIMENSION; ++n) {
#pragma HLS PIPELINE II=1

    // Read one complex sample (64-bit) from each stream
    axis_pkt p0  = s0.read();
    axis_pkt p1  = s1.read();
    axis_pkt p2  = s2.read();
    axis_pkt p3  = s3.read();
    axis_pkt p4  = s4.read();
    axis_pkt p5  = s5.read();
    axis_pkt p6  = s6.read();
    axis_pkt p7  = s7.read();
    axis_pkt p8  = s8.read();
    axis_pkt p9  = s9.read();
    axis_pkt p10 = s10.read();
    axis_pkt p11 = s11.read();
    axis_pkt p12 = s12.read();
    axis_pkt p13 = s13.read();
    axis_pkt p14 = s14.read();
    axis_pkt p15 = s15.read();

    // Twiddle for this beat: e^{-jθ} = cosθ - j sinθ
    // twiddle_re[n] = cosθ_n, twiddle_im[n] = -sinθ_n
    float c = twiddle_re[n];   // cos(θ_n)
    float s = -twiddle_im[n];  // sin(θ_n)

    // Unpack into complex floats
    float re0, im0, re1, im1, re2, im2, re3, im3;
    float re4, im4, re5, im5, re6, im6, re7, im7;
    float re8, im8, re9, im9, re10, im10, re11, im11;
    float re12, im12, re13, im13, re14, im14, re15, im15;

    unpack_complex(p0.data, re0, im0);
    unpack_complex(p1.data, re1, im1);
    unpack_complex(p2.data, re2, im2);
    unpack_complex(p3.data, re3, im3);
    unpack_complex(p4.data, re4, im4);
    unpack_complex(p5.data, re5, im5);
    unpack_complex(p6.data, re6, im6);
    unpack_complex(p7.data, re7, im7);
    unpack_complex(p8.data, re8, im8);
    unpack_complex(p9.data, re9, im9);
    unpack_complex(p10.data, re10, im10);
    unpack_complex(p11.data, re11, im11);
    unpack_complex(p12.data, re12, im12);
    unpack_complex(p13.data, re13, im13);
    unpack_complex(p14.data, re14, im14);
    unpack_complex(p15.data, re15, im15);

    // Multiply each by e^{-jθ}:
    // (re + j im) * (c - j s) = (re*c + im*s) + j(im*c - re*s)
    // We only need the real part for DCT, so only calculate (re*c + im*s)
    float out_re0  = (re0*c + im0*s);
    float out_re1  = (re1*c + im1*s);
    float out_re2  = (re2*c + im2*s);
    float out_re3  = (re3*c + im3*s);
    float out_re4  = (re4*c + im4*s);
    float out_re5  = (re5*c + im5*s);
    float out_re6  = (re6*c + im6*s);
    float out_re7  = (re7*c + im7*s);
    float out_re8  = (re8*c + im8*s);
    float out_re9  = (re9*c  + im9*s);
    float out_re10 = (re10*c + im10*s);
    float out_re11 = (re11*c + im11*s);
    float out_re12 = (re12*c + im12*s);
    float out_re13 = (re13*c + im13*s);
    float out_re14 = (re14*c + im14*s);
    float out_re15 = (re15*c + im15*s);

    // Write data back in transposed form
    ap_uint<32> output_bits[16];
#pragma HLS ARRAY_PARTITION variable=output_bits complete

    // Remove DC for second DCT pass, ie. Matrix[0][0] = 0.0f
    if (mode == TransformMode::DCT_REMOVE_DC && tile_offset == 0 && n == 0) {
      output_bits[0] = 0;  // IEEE-754 +0.0f
    } else {
      output_bits[0]  = float_to_bits(out_re0);
    }
    output_bits[1]  = float_to_bits(out_re1);
    output_bits[2]  = float_to_bits(out_re2);
    output_bits[3]  = float_to_bits(out_re3);
    output_bits[4]  = float_to_bits(out_re4);
    output_bits[5]  = float_to_bits(out_re5);
    output_bits[6]  = float_to_bits(out_re6);
    output_bits[7]  = float_to_bits(out_re7);
    output_bits[8]  = float_to_bits(out_re8);
    output_bits[9]  = float_to_bits(out_re9);
    output_bits[10] = float_to_bits(out_re10);
    output_bits[11] = float_to_bits(out_re11);
    output_bits[12] = float_to_bits(out_re12);
    output_bits[13] = float_to_bits(out_re13);
    output_bits[14] = float_to_bits(out_re14);
    output_bits[15] = float_to_bits(out_re15);

WRITE_TRANSPOSED:
    const unsigned int slot  = n & 15;
    const unsigned int block = n >> 4;

    if ((block & 1) == 0) {
      // Fill ping with the current 16x16 tile column.
FILL_PING:
      for (unsigned int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
        ping[lane].range(32 * slot + 31, 32 * slot) = output_bits[lane];
      }

      // Once the first block exists, write the previous pong bank.
      if (block != 0) {
        const unsigned int write_lane = slot;
        const unsigned int row = tile_offset * 16 + write_lane;
        const unsigned int address = mem_base +
          row * DDR_WORDS_PER_MATRIX_DIMENSION +
          (block - 1);

        mem[address] = pong[write_lane];
      }
    } else {
      // Fill pong with the current 16x16 tile column.
FILL_PONG:
      for (unsigned int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
        pong[lane].range(32 * slot + 31, 32 * slot) = output_bits[lane];
      }

      // Write one word from the previous ping bank.
      const unsigned int write_lane = slot;
      const unsigned int row = tile_offset * 16 + write_lane;
      const unsigned int address = mem_base +
        row * DDR_WORDS_PER_MATRIX_DIMENSION +
        (block - 1);

      mem[address] = ping[write_lane];
    }
  }

  // The final bank has been filled but has not yet been written.
  const unsigned int final_block = (MATRIX_DIMENSION >> 4) - 1;

DRAIN_FINAL_BANK:
  for (unsigned int lane = 0; lane < 16; ++lane) {
#pragma HLS PIPELINE II=1
    const unsigned int row = tile_offset * 16 + lane;
    const unsigned int address = mem_base +
      row * DDR_WORDS_PER_MATRIX_DIMENSION +
      final_block;

    if ((final_block & 1) == 0) {
      mem[address] = ping[lane];
    } else {
      mem[address] = pong[lane];
    }
  }

}

static bool is_idxst_mode(const TransformMode mode)
{
#pragma HLS INLINE

    return mode == TransformMode::IDXST ||
           mode == TransformMode::IDXST_FIELD_PREP;
}

void s2mm_idct_tile(
    ddr* mem,
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
#pragma HLS INLINE off

    const bool idxst = is_idxst_mode(mode);

    ap_uint<32>
        reordered[ELEMENTS_PER_DDR_WORD][MATRIX_DIMENSION];

#pragma HLS ARRAY_PARTITION variable=reordered complete dim=1
#pragma HLS ARRAY_PARTITION variable=reordered cyclic factor=16 dim=2

    constexpr float inverse_fft_scale =
        1.0f / static_cast<float>(MATRIX_DIMENSION);

IDCT_STREAM_READ_LOOP:
    for (unsigned int n = 0; n < MATRIX_DIMENSION; ++n) {
#pragma HLS PIPELINE II=1

        axis_pkt packet[ELEMENTS_PER_DDR_WORD];
#pragma HLS ARRAY_PARTITION variable=packet complete

        packet[0]  = s0.read();
        packet[1]  = s1.read();
        packet[2]  = s2.read();
        packet[3]  = s3.read();
        packet[4]  = s4.read();
        packet[5]  = s5.read();
        packet[6]  = s6.read();
        packet[7]  = s7.read();
        packet[8]  = s8.read();
        packet[9]  = s9.read();
        packet[10] = s10.read();
        packet[11] = s11.read();
        packet[12] = s12.read();
        packet[13] = s13.read();
        packet[14] = s14.read();
        packet[15] = s15.read();

        /*
         * Undo the even/odd permutation used by the DCT algorithm.
         * This gives the actual IDCT/IDXST output index k.
         */
        unsigned int logical_addr;

        if (n < (MATRIX_DIMENSION >> 1)) {
            logical_addr = 2 * n;
        } else {
            logical_addr =
                2 * MATRIX_DIMENSION - 1 - 2 * n;
        }

IDCT_UNPACK_LOOP:
        for (unsigned int lane = 0;
             lane < ELEMENTS_PER_DDR_WORD;
             ++lane) {
#pragma HLS UNROLL

            float fft_re;
            float fft_im;

            unpack_complex(
                packet[lane].data,
                fft_re,
                fft_im);

            /*
             * AIE output:
             *
             *   FFT(conj(Y)) = N * conj(IFFT(Y))
             *
             * The real IDCT result is therefore fft_re/N.
             */
            float output =
                fft_re * inverse_fft_scale;

            /*
             * IDXST(x)[k] = (-1)^k * IDCT(q)[k].
             *
             * logical_addr is k after undoing the DCT permutation.
             */
            if (idxst && (logical_addr & 1U)) {
                output = -output;
            }

            reordered[lane][logical_addr] =
                float_to_bits(output);
        }
    }

IDCT_WRITE_LANE_LOOP:
    for (unsigned int lane = 0;
         lane < ELEMENTS_PER_DDR_WORD;
         ++lane) {

IDCT_WRITE_BLOCK_LOOP:
        for (unsigned int block = 0;
             block < DDR_WORDS_PER_MATRIX_DIMENSION;
             ++block) {
#pragma HLS PIPELINE II=1

            ddr wide_word = 0;

IDCT_PACK_LOOP:
            for (unsigned int slot = 0;
                 slot < ELEMENTS_PER_DDR_WORD;
                 ++slot) {
#pragma HLS UNROLL

                const unsigned int logical_addr =
                    block * ELEMENTS_PER_DDR_WORD + slot;

                wide_word.range(
                    32 * slot + 31,
                    32 * slot) =
                    reordered[lane][logical_addr];
            }

            const unsigned int row =
                tile_offset * ELEMENTS_PER_DDR_WORD + lane;

            const unsigned int address =
                mem_base +
                row * DDR_WORDS_PER_MATRIX_DIMENSION +
                block;

            mem[address] = wide_word;
        }
    }
}

static constexpr unsigned int NUM_S2MM_WORKERS = 6;

template <unsigned int WORKER_ID>
void density_s2mm_worker(
    ddr* mem,
    const unsigned int mem_base,
    const TransformMode mode,

    // Streams from AIE to PL
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
      for (unsigned int tile_offset = WORKER_ID; tile_offset < DDR_WORDS_PER_MATRIX_DIMENSION; tile_offset+=NUM_S2MM_WORKERS ) {
        s2mm_dct_tile(mem, mem_base, tile_offset, mode,
            s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15);
      }
      break;
    case TransformMode::IDCT:
    case TransformMode::IDCT_FIELD_PREP:
    case TransformMode::IDXST:
    case TransformMode::IDXST_FIELD_PREP:
    default:
      for (unsigned int tile_offset = WORKER_ID; tile_offset < DDR_WORDS_PER_MATRIX_DIMENSION; tile_offset+=NUM_S2MM_WORKERS ) {
            s2mm_idct_tile(mem, mem_base, tile_offset, mode,
                s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15);
        }
      break;
  }

}

void density_s2mm(
    ddr* mem0,
    ddr* mem1,
    ddr* mem2,
    ddr* mem3,
    ddr* mem4,
    ddr* mem5,
    const unsigned int mem_base,
    const TransformMode mode,

    aie_stream& s0,  aie_stream& s1,  aie_stream& s2,  aie_stream& s3,  aie_stream& s4,  aie_stream& s5,  aie_stream& s6,  aie_stream& s7,
    aie_stream& s8,  aie_stream& s9,  aie_stream& s10, aie_stream& s11, aie_stream& s12, aie_stream& s13, aie_stream& s14, aie_stream& s15,
    aie_stream& s16, aie_stream& s17, aie_stream& s18, aie_stream& s19, aie_stream& s20, aie_stream& s21, aie_stream& s22, aie_stream& s23,
    aie_stream& s24, aie_stream& s25, aie_stream& s26, aie_stream& s27, aie_stream& s28, aie_stream& s29, aie_stream& s30, aie_stream& s31,
    aie_stream& s32, aie_stream& s33, aie_stream& s34, aie_stream& s35, aie_stream& s36, aie_stream& s37, aie_stream& s38, aie_stream& s39,
    aie_stream& s40, aie_stream& s41, aie_stream& s42, aie_stream& s43, aie_stream& s44, aie_stream& s45, aie_stream& s46, aie_stream& s47,
    aie_stream& s48, aie_stream& s49, aie_stream& s50, aie_stream& s51, aie_stream& s52, aie_stream& s53, aie_stream& s54, aie_stream& s55,
    aie_stream& s56, aie_stream& s57, aie_stream& s58, aie_stream& s59, aie_stream& s60, aie_stream& s61, aie_stream& s62, aie_stream& s63,
    aie_stream& s64, aie_stream& s65, aie_stream& s66, aie_stream& s67, aie_stream& s68, aie_stream& s69, aie_stream& s70, aie_stream& s71,
    aie_stream& s72, aie_stream& s73, aie_stream& s74, aie_stream& s75, aie_stream& s76, aie_stream& s77, aie_stream& s78, aie_stream& s79,
    aie_stream& s80, aie_stream& s81, aie_stream& s82, aie_stream& s83, aie_stream& s84, aie_stream& s85, aie_stream& s86, aie_stream& s87,
    aie_stream& s88, aie_stream& s89, aie_stream& s90, aie_stream& s91, aie_stream& s92, aie_stream& s93, aie_stream& s94, aie_stream& s95
    ) {
#pragma HLS DATAFLOW

    density_s2mm_worker<0>(
        mem0, mem_base, mode,
        s0,  s1,  s2,  s3,
        s4,  s5,  s6,  s7,
        s8,  s9,  s10, s11,
        s12, s13, s14, s15);

    density_s2mm_worker<1>(
        mem1, mem_base, mode,
        s16, s17, s18, s19,
        s20, s21, s22, s23,
        s24, s25, s26, s27,
        s28, s29, s30, s31);

    density_s2mm_worker<2>(
        mem2, mem_base, mode,
        s32, s33, s34, s35,
        s36, s37, s38, s39,
        s40, s41, s42, s43,
        s44, s45, s46, s47);

    density_s2mm_worker<3>(
        mem3, mem_base, mode,
        s48, s49, s50, s51,
        s52, s53, s54, s55,
        s56, s57, s58, s59,
        s60, s61, s62, s63);

    density_s2mm_worker<4>(
        mem4, mem_base, mode,
        s64, s65, s66, s67,
        s68, s69, s70, s71,
        s72, s73, s74, s75,
        s76, s77, s78, s79);

    density_s2mm_worker<5>(
        mem5, mem_base, mode,
        s80, s81, s82, s83,
        s84, s85, s86, s87,
        s88, s89, s90, s91,
        s92, s93, s94, s95);
}
