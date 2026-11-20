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
constexpr unsigned FIELD_X_BASE    = 4 * REGION_DDR_WORDS;
constexpr unsigned FIELD_Y_BASE    = 5 * REGION_DDR_WORDS;

constexpr unsigned TOTAL_DDR_WORDS = 6 * REGION_DDR_WORDS;

enum class TransformMode : std::uint8_t {
  DCT = 0,
  DCT_REMOVE_DC = 1,
  IDCT = 2,
  IDCT_FIELD_PREP = 3,
  IDXST = 4,
  IDXST_FIELD_PREP = 5,
};

enum class DensityState : std::uint8_t {
  IDLE = 0,

  DCT_PASS_1 = 1,
  DCT_PASS_2 = 2,

  DENSITY_PASS_1 = 3,
  DENSITY_PASS_2 = 4,

  FIELD_X_PASS_1 = 5,
  FIELD_X_PASS_2 = 6,

  FIELD_Y_PASS_1 = 7,
  FIELD_Y_PASS_2 = 8
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

void density_mm2s(
    const ddr* mem0,
    const ddr* mem1,
    const ddr* mem2,
    const ddr* mem3,
    const ddr* mem4,
    const ddr* mem5,
    const unsigned int mem_base,
    const TransformMode mode,

    // Streams from PL to AIE
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
    );

void density_s2mm(
    ddr* mem0,
    ddr* mem1,
    ddr* mem2,
    ddr* mem3,
    ddr* mem4,
    ddr* mem5,
    const unsigned int mem_base,
    const TransformMode mode,

    // Streams from AIE to PL
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
    );

