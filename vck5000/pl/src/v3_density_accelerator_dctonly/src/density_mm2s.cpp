#include "include.h"

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
      break;
    case TransformMode::IDXST:
      break;
  }

}

