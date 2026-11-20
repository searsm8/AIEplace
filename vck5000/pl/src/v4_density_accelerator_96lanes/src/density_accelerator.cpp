#include "include.h"


void run_transform(
    const ddr* mem_read0,
    const ddr* mem_read1,
    const ddr* mem_read2,
    const ddr* mem_read3,
    const ddr* mem_read4,
    const ddr* mem_read5,

    ddr* mem_write0,
    ddr* mem_write1,
    ddr* mem_write2,
    ddr* mem_write3,
    ddr* mem_write4,
    ddr* mem_write5,

    unsigned int src_base,
    unsigned int dst_base,
    TransformMode mode,

    // 96 streams from PL to AIE
    aie_stream& o0,  aie_stream& o1,  aie_stream& o2,  aie_stream& o3,  aie_stream& o4,  aie_stream& o5,  aie_stream& o6,  aie_stream& o7,
    aie_stream& o8,  aie_stream& o9,  aie_stream& o10, aie_stream& o11, aie_stream& o12, aie_stream& o13, aie_stream& o14, aie_stream& o15,
    aie_stream& o16, aie_stream& o17, aie_stream& o18, aie_stream& o19, aie_stream& o20, aie_stream& o21, aie_stream& o22, aie_stream& o23,
    aie_stream& o24, aie_stream& o25, aie_stream& o26, aie_stream& o27, aie_stream& o28, aie_stream& o29, aie_stream& o30, aie_stream& o31,
    aie_stream& o32, aie_stream& o33, aie_stream& o34, aie_stream& o35, aie_stream& o36, aie_stream& o37, aie_stream& o38, aie_stream& o39,
    aie_stream& o40, aie_stream& o41, aie_stream& o42, aie_stream& o43, aie_stream& o44, aie_stream& o45, aie_stream& o46, aie_stream& o47,
    aie_stream& o48, aie_stream& o49, aie_stream& o50, aie_stream& o51, aie_stream& o52, aie_stream& o53, aie_stream& o54, aie_stream& o55,
    aie_stream& o56, aie_stream& o57, aie_stream& o58, aie_stream& o59, aie_stream& o60, aie_stream& o61, aie_stream& o62, aie_stream& o63,
    aie_stream& o64, aie_stream& o65, aie_stream& o66, aie_stream& o67, aie_stream& o68, aie_stream& o69, aie_stream& o70, aie_stream& o71,
    aie_stream& o72, aie_stream& o73, aie_stream& o74, aie_stream& o75, aie_stream& o76, aie_stream& o77, aie_stream& o78, aie_stream& o79,
    aie_stream& o80, aie_stream& o81, aie_stream& o82, aie_stream& o83, aie_stream& o84, aie_stream& o85, aie_stream& o86, aie_stream& o87,
    aie_stream& o88, aie_stream& o89, aie_stream& o90, aie_stream& o91, aie_stream& o92, aie_stream& o93, aie_stream& o94, aie_stream& o95,

    // 96 streams from AIE to PL
    aie_stream& i0,  aie_stream& i1,  aie_stream& i2,  aie_stream& i3,  aie_stream& i4,  aie_stream& i5,  aie_stream& i6,  aie_stream& i7,
    aie_stream& i8,  aie_stream& i9,  aie_stream& i10, aie_stream& i11, aie_stream& i12, aie_stream& i13, aie_stream& i14, aie_stream& i15,
    aie_stream& i16, aie_stream& i17, aie_stream& i18, aie_stream& i19, aie_stream& i20, aie_stream& i21, aie_stream& i22, aie_stream& i23,
    aie_stream& i24, aie_stream& i25, aie_stream& i26, aie_stream& i27, aie_stream& i28, aie_stream& i29, aie_stream& i30, aie_stream& i31,
    aie_stream& i32, aie_stream& i33, aie_stream& i34, aie_stream& i35, aie_stream& i36, aie_stream& i37, aie_stream& i38, aie_stream& i39,
    aie_stream& i40, aie_stream& i41, aie_stream& i42, aie_stream& i43, aie_stream& i44, aie_stream& i45, aie_stream& i46, aie_stream& i47,
    aie_stream& i48, aie_stream& i49, aie_stream& i50, aie_stream& i51, aie_stream& i52, aie_stream& i53, aie_stream& i54, aie_stream& i55,
    aie_stream& i56, aie_stream& i57, aie_stream& i58, aie_stream& i59, aie_stream& i60, aie_stream& i61, aie_stream& i62, aie_stream& i63,
    aie_stream& i64, aie_stream& i65, aie_stream& i66, aie_stream& i67, aie_stream& i68, aie_stream& i69, aie_stream& i70, aie_stream& i71,
    aie_stream& i72, aie_stream& i73, aie_stream& i74, aie_stream& i75, aie_stream& i76, aie_stream& i77, aie_stream& i78, aie_stream& i79,
    aie_stream& i80, aie_stream& i81, aie_stream& i82, aie_stream& i83, aie_stream& i84, aie_stream& i85, aie_stream& i86, aie_stream& i87,
    aie_stream& i88, aie_stream& i89, aie_stream& i90, aie_stream& i91, aie_stream& i92, aie_stream& i93, aie_stream& i94, aie_stream& i95
    ) {

#pragma HLS DATAFLOW
      density_mm2s(
          mem_read0, mem_read1, mem_read2, mem_read3, mem_read4, mem_read5,
          src_base, mode,
          o0,  o1,  o2,  o3,  o4,  o5,  o6,  o7,
          o8,  o9,  o10, o11, o12, o13, o14, o15,
          o16, o17, o18, o19, o20, o21, o22, o23,
          o24, o25, o26, o27, o28, o29, o30, o31,
          o32, o33, o34, o35, o36, o37, o38, o39,
          o40, o41, o42, o43, o44, o45, o46, o47,
          o48, o49, o50, o51, o52, o53, o54, o55,
          o56, o57, o58, o59, o60, o61, o62, o63,
          o64, o65, o66, o67, o68, o69, o70, o71,
          o72, o73, o74, o75, o76, o77, o78, o79,
          o80, o81, o82, o83, o84, o85, o86, o87,
          o88, o89, o90, o91, o92, o93, o94, o95);

      density_s2mm(
          mem_write0, mem_write1, mem_write2, mem_write3, mem_write4, mem_write5,
          dst_base, mode,
          i0,  i1,  i2,  i3,  i4,  i5,  i6,  i7,
          i8,  i9,  i10, i11, i12, i13, i14, i15,
          i16, i17, i18, i19, i20, i21, i22, i23,
          i24, i25, i26, i27, i28, i29, i30, i31,
          i32, i33, i34, i35, i36, i37, i38, i39,
          i40, i41, i42, i43, i44, i45, i46, i47,
          i48, i49, i50, i51, i52, i53, i54, i55,
          i56, i57, i58, i59, i60, i61, i62, i63,
          i64, i65, i66, i67, i68, i69, i70, i71,
          i72, i73, i74, i75, i76, i77, i78, i79,
          i80, i81, i82, i83, i84, i85, i86, i87,
          i88, i89, i90, i91, i92, i93, i94, i95);
    }

extern "C" void density_accelerator(
    const ddr* mem_read0,
    const ddr* mem_read1,
    const ddr* mem_read2,
    const ddr* mem_read3,
    const ddr* mem_read4,
    const ddr* mem_read5,

    ddr* mem_write0,
    ddr* mem_write1,
    ddr* mem_write2,
    ddr* mem_write3,
    ddr* mem_write4,
    ddr* mem_write5,

    // 96 streams from PL to AIE
    aie_stream& o0,  aie_stream& o1,  aie_stream& o2,  aie_stream& o3,  aie_stream& o4,  aie_stream& o5,  aie_stream& o6,  aie_stream& o7,
    aie_stream& o8,  aie_stream& o9,  aie_stream& o10, aie_stream& o11, aie_stream& o12, aie_stream& o13, aie_stream& o14, aie_stream& o15,
    aie_stream& o16, aie_stream& o17, aie_stream& o18, aie_stream& o19, aie_stream& o20, aie_stream& o21, aie_stream& o22, aie_stream& o23,
    aie_stream& o24, aie_stream& o25, aie_stream& o26, aie_stream& o27, aie_stream& o28, aie_stream& o29, aie_stream& o30, aie_stream& o31,
    aie_stream& o32, aie_stream& o33, aie_stream& o34, aie_stream& o35, aie_stream& o36, aie_stream& o37, aie_stream& o38, aie_stream& o39,
    aie_stream& o40, aie_stream& o41, aie_stream& o42, aie_stream& o43, aie_stream& o44, aie_stream& o45, aie_stream& o46, aie_stream& o47,
    aie_stream& o48, aie_stream& o49, aie_stream& o50, aie_stream& o51, aie_stream& o52, aie_stream& o53, aie_stream& o54, aie_stream& o55,
    aie_stream& o56, aie_stream& o57, aie_stream& o58, aie_stream& o59, aie_stream& o60, aie_stream& o61, aie_stream& o62, aie_stream& o63,
    aie_stream& o64, aie_stream& o65, aie_stream& o66, aie_stream& o67, aie_stream& o68, aie_stream& o69, aie_stream& o70, aie_stream& o71,
    aie_stream& o72, aie_stream& o73, aie_stream& o74, aie_stream& o75, aie_stream& o76, aie_stream& o77, aie_stream& o78, aie_stream& o79,
    aie_stream& o80, aie_stream& o81, aie_stream& o82, aie_stream& o83, aie_stream& o84, aie_stream& o85, aie_stream& o86, aie_stream& o87,
    aie_stream& o88, aie_stream& o89, aie_stream& o90, aie_stream& o91, aie_stream& o92, aie_stream& o93, aie_stream& o94, aie_stream& o95,

    // 96 streams from AIE to PL
    aie_stream& i0,  aie_stream& i1,  aie_stream& i2,  aie_stream& i3,  aie_stream& i4,  aie_stream& i5,  aie_stream& i6,  aie_stream& i7,
    aie_stream& i8,  aie_stream& i9,  aie_stream& i10, aie_stream& i11, aie_stream& i12, aie_stream& i13, aie_stream& i14, aie_stream& i15,
    aie_stream& i16, aie_stream& i17, aie_stream& i18, aie_stream& i19, aie_stream& i20, aie_stream& i21, aie_stream& i22, aie_stream& i23,
    aie_stream& i24, aie_stream& i25, aie_stream& i26, aie_stream& i27, aie_stream& i28, aie_stream& i29, aie_stream& i30, aie_stream& i31,
    aie_stream& i32, aie_stream& i33, aie_stream& i34, aie_stream& i35, aie_stream& i36, aie_stream& i37, aie_stream& i38, aie_stream& i39,
    aie_stream& i40, aie_stream& i41, aie_stream& i42, aie_stream& i43, aie_stream& i44, aie_stream& i45, aie_stream& i46, aie_stream& i47,
    aie_stream& i48, aie_stream& i49, aie_stream& i50, aie_stream& i51, aie_stream& i52, aie_stream& i53, aie_stream& i54, aie_stream& i55,
    aie_stream& i56, aie_stream& i57, aie_stream& i58, aie_stream& i59, aie_stream& i60, aie_stream& i61, aie_stream& i62, aie_stream& i63,
    aie_stream& i64, aie_stream& i65, aie_stream& i66, aie_stream& i67, aie_stream& i68, aie_stream& i69, aie_stream& i70, aie_stream& i71,
    aie_stream& i72, aie_stream& i73, aie_stream& i74, aie_stream& i75, aie_stream& i76, aie_stream& i77, aie_stream& i78, aie_stream& i79,
    aie_stream& i80, aie_stream& i81, aie_stream& i82, aie_stream& i83, aie_stream& i84, aie_stream& i85, aie_stream& i86, aie_stream& i87,
    aie_stream& i88, aie_stream& i89, aie_stream& i90, aie_stream& i91, aie_stream& i92, aie_stream& i93, aie_stream& i94, aie_stream& i95
    ) {
#pragma HLS INTERFACE m_axi port=mem_read0  bundle=gmem_r0
#pragma HLS INTERFACE m_axi port=mem_read1  bundle=gmem_r1
#pragma HLS INTERFACE m_axi port=mem_read2  bundle=gmem_r2
#pragma HLS INTERFACE m_axi port=mem_read3  bundle=gmem_r3
#pragma HLS INTERFACE m_axi port=mem_read4  bundle=gmem_r4
#pragma HLS INTERFACE m_axi port=mem_read5  bundle=gmem_r5

#pragma HLS INTERFACE m_axi port=mem_write0 bundle=gmem_w0
#pragma HLS INTERFACE m_axi port=mem_write1 bundle=gmem_w1
#pragma HLS INTERFACE m_axi port=mem_write2 bundle=gmem_w2
#pragma HLS INTERFACE m_axi port=mem_write3 bundle=gmem_w3
#pragma HLS INTERFACE m_axi port=mem_write4 bundle=gmem_w4
#pragma HLS INTERFACE m_axi port=mem_write5 bundle=gmem_w5

#pragma HLS INTERFACE s_axilite port=mem_read0  bundle=control
#pragma HLS INTERFACE s_axilite port=mem_read1  bundle=control
#pragma HLS INTERFACE s_axilite port=mem_read2  bundle=control
#pragma HLS INTERFACE s_axilite port=mem_read3  bundle=control
#pragma HLS INTERFACE s_axilite port=mem_read4  bundle=control
#pragma HLS INTERFACE s_axilite port=mem_read5  bundle=control

#pragma HLS INTERFACE s_axilite port=mem_write0 bundle=control
#pragma HLS INTERFACE s_axilite port=mem_write1 bundle=control
#pragma HLS INTERFACE s_axilite port=mem_write2 bundle=control
#pragma HLS INTERFACE s_axilite port=mem_write3 bundle=control
#pragma HLS INTERFACE s_axilite port=mem_write4 bundle=control
#pragma HLS INTERFACE s_axilite port=mem_write5 bundle=control

#pragma HLS INTERFACE s_axilite port=return    bundle=control

#pragma HLS INTERFACE axis port=o0
#pragma HLS INTERFACE axis port=o1
#pragma HLS INTERFACE axis port=o2
#pragma HLS INTERFACE axis port=o3
#pragma HLS INTERFACE axis port=o4
#pragma HLS INTERFACE axis port=o5
#pragma HLS INTERFACE axis port=o6
#pragma HLS INTERFACE axis port=o7
#pragma HLS INTERFACE axis port=o8
#pragma HLS INTERFACE axis port=o9
#pragma HLS INTERFACE axis port=o10
#pragma HLS INTERFACE axis port=o11
#pragma HLS INTERFACE axis port=o12
#pragma HLS INTERFACE axis port=o13
#pragma HLS INTERFACE axis port=o14
#pragma HLS INTERFACE axis port=o15
#pragma HLS INTERFACE axis port=o16
#pragma HLS INTERFACE axis port=o17
#pragma HLS INTERFACE axis port=o18
#pragma HLS INTERFACE axis port=o19
#pragma HLS INTERFACE axis port=o20
#pragma HLS INTERFACE axis port=o21
#pragma HLS INTERFACE axis port=o22
#pragma HLS INTERFACE axis port=o23
#pragma HLS INTERFACE axis port=o24
#pragma HLS INTERFACE axis port=o25
#pragma HLS INTERFACE axis port=o26
#pragma HLS INTERFACE axis port=o27
#pragma HLS INTERFACE axis port=o28
#pragma HLS INTERFACE axis port=o29
#pragma HLS INTERFACE axis port=o30
#pragma HLS INTERFACE axis port=o31
#pragma HLS INTERFACE axis port=o32
#pragma HLS INTERFACE axis port=o33
#pragma HLS INTERFACE axis port=o34
#pragma HLS INTERFACE axis port=o35
#pragma HLS INTERFACE axis port=o36
#pragma HLS INTERFACE axis port=o37
#pragma HLS INTERFACE axis port=o38
#pragma HLS INTERFACE axis port=o39
#pragma HLS INTERFACE axis port=o40
#pragma HLS INTERFACE axis port=o41
#pragma HLS INTERFACE axis port=o42
#pragma HLS INTERFACE axis port=o43
#pragma HLS INTERFACE axis port=o44
#pragma HLS INTERFACE axis port=o45
#pragma HLS INTERFACE axis port=o46
#pragma HLS INTERFACE axis port=o47
#pragma HLS INTERFACE axis port=o48
#pragma HLS INTERFACE axis port=o49
#pragma HLS INTERFACE axis port=o50
#pragma HLS INTERFACE axis port=o51
#pragma HLS INTERFACE axis port=o52
#pragma HLS INTERFACE axis port=o53
#pragma HLS INTERFACE axis port=o54
#pragma HLS INTERFACE axis port=o55
#pragma HLS INTERFACE axis port=o56
#pragma HLS INTERFACE axis port=o57
#pragma HLS INTERFACE axis port=o58
#pragma HLS INTERFACE axis port=o59
#pragma HLS INTERFACE axis port=o60
#pragma HLS INTERFACE axis port=o61
#pragma HLS INTERFACE axis port=o62
#pragma HLS INTERFACE axis port=o63
#pragma HLS INTERFACE axis port=o64
#pragma HLS INTERFACE axis port=o65
#pragma HLS INTERFACE axis port=o66
#pragma HLS INTERFACE axis port=o67
#pragma HLS INTERFACE axis port=o68
#pragma HLS INTERFACE axis port=o69
#pragma HLS INTERFACE axis port=o70
#pragma HLS INTERFACE axis port=o71
#pragma HLS INTERFACE axis port=o72
#pragma HLS INTERFACE axis port=o73
#pragma HLS INTERFACE axis port=o74
#pragma HLS INTERFACE axis port=o75
#pragma HLS INTERFACE axis port=o76
#pragma HLS INTERFACE axis port=o77
#pragma HLS INTERFACE axis port=o78
#pragma HLS INTERFACE axis port=o79
#pragma HLS INTERFACE axis port=o80
#pragma HLS INTERFACE axis port=o81
#pragma HLS INTERFACE axis port=o82
#pragma HLS INTERFACE axis port=o83
#pragma HLS INTERFACE axis port=o84
#pragma HLS INTERFACE axis port=o85
#pragma HLS INTERFACE axis port=o86
#pragma HLS INTERFACE axis port=o87
#pragma HLS INTERFACE axis port=o88
#pragma HLS INTERFACE axis port=o89
#pragma HLS INTERFACE axis port=o90
#pragma HLS INTERFACE axis port=o91
#pragma HLS INTERFACE axis port=o92
#pragma HLS INTERFACE axis port=o93
#pragma HLS INTERFACE axis port=o94
#pragma HLS INTERFACE axis port=o95

#pragma HLS INTERFACE axis port=i0
#pragma HLS INTERFACE axis port=i1
#pragma HLS INTERFACE axis port=i2
#pragma HLS INTERFACE axis port=i3
#pragma HLS INTERFACE axis port=i4
#pragma HLS INTERFACE axis port=i5
#pragma HLS INTERFACE axis port=i6
#pragma HLS INTERFACE axis port=i7
#pragma HLS INTERFACE axis port=i8
#pragma HLS INTERFACE axis port=i9
#pragma HLS INTERFACE axis port=i10
#pragma HLS INTERFACE axis port=i11
#pragma HLS INTERFACE axis port=i12
#pragma HLS INTERFACE axis port=i13
#pragma HLS INTERFACE axis port=i14
#pragma HLS INTERFACE axis port=i15
#pragma HLS INTERFACE axis port=i16
#pragma HLS INTERFACE axis port=i17
#pragma HLS INTERFACE axis port=i18
#pragma HLS INTERFACE axis port=i19
#pragma HLS INTERFACE axis port=i20
#pragma HLS INTERFACE axis port=i21
#pragma HLS INTERFACE axis port=i22
#pragma HLS INTERFACE axis port=i23
#pragma HLS INTERFACE axis port=i24
#pragma HLS INTERFACE axis port=i25
#pragma HLS INTERFACE axis port=i26
#pragma HLS INTERFACE axis port=i27
#pragma HLS INTERFACE axis port=i28
#pragma HLS INTERFACE axis port=i29
#pragma HLS INTERFACE axis port=i30
#pragma HLS INTERFACE axis port=i31
#pragma HLS INTERFACE axis port=i32
#pragma HLS INTERFACE axis port=i33
#pragma HLS INTERFACE axis port=i34
#pragma HLS INTERFACE axis port=i35
#pragma HLS INTERFACE axis port=i36
#pragma HLS INTERFACE axis port=i37
#pragma HLS INTERFACE axis port=i38
#pragma HLS INTERFACE axis port=i39
#pragma HLS INTERFACE axis port=i40
#pragma HLS INTERFACE axis port=i41
#pragma HLS INTERFACE axis port=i42
#pragma HLS INTERFACE axis port=i43
#pragma HLS INTERFACE axis port=i44
#pragma HLS INTERFACE axis port=i45
#pragma HLS INTERFACE axis port=i46
#pragma HLS INTERFACE axis port=i47
#pragma HLS INTERFACE axis port=i48
#pragma HLS INTERFACE axis port=i49
#pragma HLS INTERFACE axis port=i50
#pragma HLS INTERFACE axis port=i51
#pragma HLS INTERFACE axis port=i52
#pragma HLS INTERFACE axis port=i53
#pragma HLS INTERFACE axis port=i54
#pragma HLS INTERFACE axis port=i55
#pragma HLS INTERFACE axis port=i56
#pragma HLS INTERFACE axis port=i57
#pragma HLS INTERFACE axis port=i58
#pragma HLS INTERFACE axis port=i59
#pragma HLS INTERFACE axis port=i60
#pragma HLS INTERFACE axis port=i61
#pragma HLS INTERFACE axis port=i62
#pragma HLS INTERFACE axis port=i63
#pragma HLS INTERFACE axis port=i64
#pragma HLS INTERFACE axis port=i65
#pragma HLS INTERFACE axis port=i66
#pragma HLS INTERFACE axis port=i67
#pragma HLS INTERFACE axis port=i68
#pragma HLS INTERFACE axis port=i69
#pragma HLS INTERFACE axis port=i70
#pragma HLS INTERFACE axis port=i71
#pragma HLS INTERFACE axis port=i72
#pragma HLS INTERFACE axis port=i73
#pragma HLS INTERFACE axis port=i74
#pragma HLS INTERFACE axis port=i75
#pragma HLS INTERFACE axis port=i76
#pragma HLS INTERFACE axis port=i77
#pragma HLS INTERFACE axis port=i78
#pragma HLS INTERFACE axis port=i79
#pragma HLS INTERFACE axis port=i80
#pragma HLS INTERFACE axis port=i81
#pragma HLS INTERFACE axis port=i82
#pragma HLS INTERFACE axis port=i83
#pragma HLS INTERFACE axis port=i84
#pragma HLS INTERFACE axis port=i85
#pragma HLS INTERFACE axis port=i86
#pragma HLS INTERFACE axis port=i87
#pragma HLS INTERFACE axis port=i88
#pragma HLS INTERFACE axis port=i89
#pragma HLS INTERFACE axis port=i90
#pragma HLS INTERFACE axis port=i91
#pragma HLS INTERFACE axis port=i92
#pragma HLS INTERFACE axis port=i93
#pragma HLS INTERFACE axis port=i94
#pragma HLS INTERFACE axis port=i95

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

    run_transform(
    mem_read0, mem_read1, mem_read2, mem_read3, mem_read4, mem_read5,
    mem_write0, mem_write1, mem_write2, mem_write3, mem_write4, mem_write5,
    src_base, dst_base, mode,
    o0,  o1,  o2,  o3,  o4,  o5,  o6,  o7,
    o8,  o9,  o10, o11, o12, o13, o14, o15,
    o16, o17, o18, o19, o20, o21, o22, o23,
    o24, o25, o26, o27, o28, o29, o30, o31,
    o32, o33, o34, o35, o36, o37, o38, o39,
    o40, o41, o42, o43, o44, o45, o46, o47,
    o48, o49, o50, o51, o52, o53, o54, o55,
    o56, o57, o58, o59, o60, o61, o62, o63,
    o64, o65, o66, o67, o68, o69, o70, o71,
    o72, o73, o74, o75, o76, o77, o78, o79,
    o80, o81, o82, o83, o84, o85, o86, o87,
    o88, o89, o90, o91, o92, o93, o94, o95,

    i0,  i1,  i2,  i3,  i4,  i5,  i6,  i7,
    i8,  i9,  i10, i11, i12, i13, i14, i15,
    i16, i17, i18, i19, i20, i21, i22, i23,
    i24, i25, i26, i27, i28, i29, i30, i31,
    i32, i33, i34, i35, i36, i37, i38, i39,
    i40, i41, i42, i43, i44, i45, i46, i47,
    i48, i49, i50, i51, i52, i53, i54, i55,
    i56, i57, i58, i59, i60, i61, i62, i63,
    i64, i65, i66, i67, i68, i69, i70, i71,
    i72, i73, i74, i75, i76, i77, i78, i79,
    i80, i81, i82, i83, i84, i85, i86, i87,
    i88, i89, i90, i91, i92, i93, i94, i95);

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

