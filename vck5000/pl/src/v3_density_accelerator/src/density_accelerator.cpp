#include "include.h"


void run_transform(
    const ddr* mem_read,
    ddr* mem_write,
    unsigned int src_base,
    unsigned int dst_base,
    TransformMode mode,

    // 16 streams from PL to AIE
    aie_stream& o0,  aie_stream& o1,
    aie_stream& o2,  aie_stream& o3,
    aie_stream& o4,  aie_stream& o5,
    aie_stream& o6,  aie_stream& o7,
    aie_stream& o8,  aie_stream& o9,
    aie_stream& o10, aie_stream& o11,
    aie_stream& o12, aie_stream& o13,
    aie_stream& o14, aie_stream& o15,
    // 16 streams from AIE to PL
    aie_stream& i0,  aie_stream& i1,
    aie_stream& i2,  aie_stream& i3,
    aie_stream& i4,  aie_stream& i5,
    aie_stream& i6,  aie_stream& i7,
    aie_stream& i8,  aie_stream& i9,
    aie_stream& i10, aie_stream& i11,
    aie_stream& i12, aie_stream& i13,
    aie_stream& i14, aie_stream& i15
    ) {

#pragma HLS DATAFLOW
      density_mm2s(mem_read, src_base, mode, o0,o1,o2,o3,o4,o5,o6,o7,o8,o9,o10,o11,o12,o13,o14,o15);
      density_s2mm(mem_write, dst_base, mode, i0,i1,i2,i3,i4,i5,i6,i7,i8,i9,i10,i11,i12,i13,i14,i15);
    }

extern "C" void density_accelerator(
    const ddr* mem_read,
    ddr* mem_write,

    // 16 streams from PL to AIE
    aie_stream& o0,  aie_stream& o1,
    aie_stream& o2,  aie_stream& o3,
    aie_stream& o4,  aie_stream& o5,
    aie_stream& o6,  aie_stream& o7,
    aie_stream& o8,  aie_stream& o9,
    aie_stream& o10, aie_stream& o11,
    aie_stream& o12, aie_stream& o13,
    aie_stream& o14, aie_stream& o15,
    // 16 streams from AIE to PL
    aie_stream& i0,  aie_stream& i1,
    aie_stream& i2,  aie_stream& i3,
    aie_stream& i4,  aie_stream& i5,
    aie_stream& i6,  aie_stream& i7,
    aie_stream& i8,  aie_stream& i9,
    aie_stream& i10, aie_stream& i11,
    aie_stream& i12, aie_stream& i13,
    aie_stream& i14, aie_stream& i15)
{
#pragma HLS INTERFACE m_axi port=mem_read  offset=slave bundle=gmem depth=TOTAL_DDR_WORDS
#pragma HLS INTERFACE m_axi port=mem_write offset=slave bundle=gmem depth=TOTAL_DDR_WORDS

#pragma HLS INTERFACE s_axilite port=mem_read  bundle=control
#pragma HLS INTERFACE s_axilite port=mem_write bundle=control
#pragma HLS INTERFACE s_axilite port=return    bundle=control

#pragma HLS INTERFACE axis      port=o0
#pragma HLS INTERFACE axis      port=o1
#pragma HLS INTERFACE axis      port=o2
#pragma HLS INTERFACE axis      port=o3
#pragma HLS INTERFACE axis      port=o4
#pragma HLS INTERFACE axis      port=o5
#pragma HLS INTERFACE axis      port=o6
#pragma HLS INTERFACE axis      port=o7
#pragma HLS INTERFACE axis      port=o8
#pragma HLS INTERFACE axis      port=o9
#pragma HLS INTERFACE axis      port=o10
#pragma HLS INTERFACE axis      port=o11
#pragma HLS INTERFACE axis      port=o12
#pragma HLS INTERFACE axis      port=o13
#pragma HLS INTERFACE axis      port=o14
#pragma HLS INTERFACE axis      port=o15

#pragma HLS INTERFACE axis      port=i0
#pragma HLS INTERFACE axis      port=i1
#pragma HLS INTERFACE axis      port=i2
#pragma HLS INTERFACE axis      port=i3
#pragma HLS INTERFACE axis      port=i4
#pragma HLS INTERFACE axis      port=i5
#pragma HLS INTERFACE axis      port=i6
#pragma HLS INTERFACE axis      port=i7
#pragma HLS INTERFACE axis      port=i8
#pragma HLS INTERFACE axis      port=i9
#pragma HLS INTERFACE axis      port=i10
#pragma HLS INTERFACE axis      port=i11
#pragma HLS INTERFACE axis      port=i12
#pragma HLS INTERFACE axis      port=i13
#pragma HLS INTERFACE axis      port=i14
#pragma HLS INTERFACE axis      port=i15

ITERATION_LOOP:
  DensityState state = DensityState::DCT_PASS_1;

  while (state != DensityState::IDLE) {
    unsigned int    src_base  = INPUT_BASE;
    unsigned int    dst_base  = TMP_BASE;
    TransformMode   mode = TransformMode::DCT;

    /*
     * This switch only selects the arguments for the one shared
     * run_transform() call below.
     */
    switch (state) {
      case DensityState::DCT_PASS_1:
        src_base  = INPUT_BASE;
        dst_base  = TMP_BASE;
        mode = TransformMode::DCT;
        break;

      case DensityState::DCT_PASS_2:
        src_base  = TMP_BASE;
        dst_base  = FREQ_NO_DC_BASE;
        mode = TransformMode::DCT_REMOVE_DC;
        break;

      case DensityState::DENSITY_PASS_1:
        src_base  = FREQ_NO_DC_BASE;
        dst_base  = TMP_BASE;
        mode = TransformMode::IDCT;
        break;

      case DensityState::DENSITY_PASS_2:
        src_base  = TMP_BASE;
        dst_base  = DENSITY_BASE;
        mode = TransformMode::IDCT;
        break;

      case DensityState::FIELD_X_PASS_1:
        src_base  = FREQ_NO_DC_BASE;
        dst_base  = TMP_BASE;
        mode = TransformMode::IDCT_FIELD_PREP;
        break;

      case DensityState::FIELD_X_PASS_2:
        src_base  = TMP_BASE;
        dst_base  = FIELD_X_BASE;
        mode = TransformMode::IDXST;
        break;

      case DensityState::FIELD_Y_PASS_1:
        src_base  = FREQ_NO_DC_BASE;
        dst_base  = TMP_BASE;
        mode = TransformMode::IDXST_FIELD_PREP;
        break;

      case DensityState::FIELD_Y_PASS_2:
        src_base  = TMP_BASE;
        dst_base  = FIELD_Y_BASE;
        mode = TransformMode::IDCT;
        break;

      case DensityState::IDLE:
        break;
    }

    run_transform(mem_read, mem_write, src_base, dst_base, mode,
        o0,o1,o2,o3,o4,o5,o6,o7,o8,o9,o10,o11,o12,o13,o14,o15,
        i0,i1,i2,i3,i4,i5,i6,i7,i8,i9,i10,i11,i12,i13,i14,i15);

    // After transform set next state
    switch (state) {
      case DensityState::DCT_PASS_1:
        state = DensityState::DCT_PASS_2;
        break;

      case DensityState::DCT_PASS_2:
        state = DensityState::DENSITY_PASS_1;
        break;

      case DensityState::DENSITY_PASS_1:
        state = DensityState::DENSITY_PASS_2;
        break;

      case DensityState::DENSITY_PASS_2:
        state = DensityState::FIELD_X_PASS_1;
        break;

      case DensityState::FIELD_X_PASS_1:
        state = DensityState::FIELD_X_PASS_2;
        break;

      case DensityState::FIELD_X_PASS_2:
        state = DensityState::FIELD_Y_PASS_1;
        break;

      case DensityState::FIELD_Y_PASS_1:
        state = DensityState::FIELD_Y_PASS_2;
        break;

      case DensityState::FIELD_Y_PASS_2:
        state = DensityState::IDLE;
        break;

      case DensityState::IDLE:
        break;
    }
  }
}

