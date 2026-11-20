#pragma once

#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>
#include <cstdint>

constexpr unsigned MATRIX_DIMENSION = 1024;
constexpr unsigned MATRIX_ELEMENTS = MATRIX_DIMENSION * MATRIX_DIMENSION;
constexpr unsigned ELEMENTS_PER_DDR_WORD = 16; // 512 / 32
constexpr unsigned DDR_WORDS_PER_MATRIX_DIMENSION = 64; // 1024 floats -> 32768 bits: 32768/512 = 64 ddr words per row/column
constexpr unsigned REGION_DDR_WORDS =
MATRIX_ELEMENTS / ELEMENTS_PER_DDR_WORD;

constexpr unsigned INPUT_BASE      = 0 * REGION_DDR_WORDS;
constexpr unsigned TMP_BASE        = 1 * REGION_DDR_WORDS;
constexpr unsigned FREQ_NO_DC_BASE = 2 * REGION_DDR_WORDS;
constexpr unsigned DENSITY_BASE    = 3 * REGION_DDR_WORDS;
constexpr unsigned GRADIENT_X_BASE = 4 * REGION_DDR_WORDS;
constexpr unsigned GRADIENT_Y_BASE = 5 * REGION_DDR_WORDS;

constexpr unsigned TOTAL_DDR_WORDS = 6 * REGION_DDR_WORDS;

enum class TransformMode : std::uint8_t {
  DCT = 0,
  DCT_REMOVE_DC = 1,
  IDCT = 2,
  IDXST = 3,
};

enum class DensityState : std::uint8_t {
  IDLE = 0,

  DCT_PASS_1 = 1,
  DCT_PASS_2 = 2,

  DENSITY_PASS_1 = 3,
  DENSITY_PASS_2 = 4,

  GRADIENT_X_PASS_1 = 5,
  GRADIENT_X_PASS_2 = 6,

  GRADIENT_Y_PASS_1 = 7,
  GRADIENT_Y_PASS_2 = 8
};

typedef ap_uint<512> ddr;                  // 512-bit DDR word
typedef ap_uint<32>  real;                  // 32-bit real float word
typedef ap_axiu<64,0,0,0> axis_pkt;           // 64-bit AXI4-Stream word
typedef hls::stream<axis_pkt> aie_stream;

union FloatBits {
  float f;
  std::uint32_t u;
};

static ap_uint<32> float_to_bits(float value) {
#pragma HLS INLINE
  FloatBits converter;
  converter.f = value;
  return converter.u;
}

static float bits_to_float(ap_uint<32> value) {
#pragma HLS INLINE
  FloatBits converter;
  converter.u = value.to_uint();
  return converter.f;
}

// Unpack 64 bits into two floats: [63:32] -> imag, [31:0] -> real
static void unpack_complex(ap_uint<64> in, float &re, float &im) {
#pragma HLS INLINE
  ap_uint<32> im_bits = in.range(63, 32);
  ap_uint<32> re_bits = in.range(31, 0);

  re = bits_to_float(re_bits);
  im = bits_to_float(im_bits);
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
    );

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
    );

