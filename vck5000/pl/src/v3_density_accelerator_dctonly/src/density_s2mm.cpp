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

void density_s2mm(ddr* mem,
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
      for (unsigned int tile_offset = 0; tile_offset < DDR_WORDS_PER_MATRIX_DIMENSION; tile_offset++ ) {
        s2mm_dct_tile(mem, mem_base, tile_offset, mode,
            s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15);
      }
      break;
    case TransformMode::IDCT:
      break;
    case TransformMode::IDXST:
      break;
  }

}

