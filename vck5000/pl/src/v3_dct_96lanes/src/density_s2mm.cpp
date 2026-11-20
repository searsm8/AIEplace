#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>
#include <cstdint>

#include "twiddle_table.h"

extern "C" {

typedef ap_uint<512> ddr;
typedef ap_axiu<64, 0, 0, 0> axis_pkt;
typedef hls::stream<axis_pkt> aie_stream;

static constexpr unsigned int NUM_LANES = 96;
static constexpr unsigned int FLOATS_PER_DDR_WORD = 16;
static constexpr unsigned int DDR_WORDS_PER_OUTPUT =
    (NUM_LANES + FLOATS_PER_DDR_WORD - 1) / FLOATS_PER_DDR_WORD;

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

// Unpack a 64-bit complex value:
//   [31:0]  -> real
//   [63:32] -> imaginary
static void unpack_complex(
    ap_uint<64> input,
    float& re,
    float& im
) {
#pragma HLS INLINE
    ap_uint<32> re_bits = input.range(31, 0);
    ap_uint<32> im_bits = input.range(63, 32);

    re = bits_to_float(re_bits);
    im = bits_to_float(im_bits);
}

// Receive one complex sample from each AXI stream for every iteration.
//
// "size" is the number of samples received from each lane.
//
// Each output sample contains one 32-bit real value. Therefore, every
// iteration writes DDR_WORDS_PER_OUTPUT 512-bit DDR words.
//
// Required DDR storage:
//     size * DDR_WORDS_PER_OUTPUT 512-bit words
//
// DDR layout for iteration n:
//     mem[n * DDR_WORDS_PER_OUTPUT + 0] -> lanes 1..16
//     mem[n * DDR_WORDS_PER_OUTPUT + 1] -> lanes 17..32
//     ...
void density_s2mm(
    ddr* mem,
    const unsigned int size,
    aie_stream& s0,    aie_stream& s1,    aie_stream& s2,    aie_stream& s3,    aie_stream& s4,    aie_stream& s5,    aie_stream& s6,    aie_stream& s7,    aie_stream& s8,    aie_stream& s9,    aie_stream& s10,    aie_stream& s11,    aie_stream& s12,    aie_stream& s13,    aie_stream& s14,    aie_stream& s15,    aie_stream& s16,    aie_stream& s17,    aie_stream& s18,    aie_stream& s19,    aie_stream& s20,    aie_stream& s21,    aie_stream& s22,    aie_stream& s23,    aie_stream& s24,    aie_stream& s25,    aie_stream& s26,    aie_stream& s27,    aie_stream& s28,    aie_stream& s29,    aie_stream& s30,    aie_stream& s31,    aie_stream& s32,    aie_stream& s33,    aie_stream& s34,    aie_stream& s35,    aie_stream& s36,    aie_stream& s37,    aie_stream& s38,    aie_stream& s39,    aie_stream& s40,    aie_stream& s41,    aie_stream& s42,    aie_stream& s43,    aie_stream& s44,    aie_stream& s45,    aie_stream& s46,    aie_stream& s47,    aie_stream& s48,    aie_stream& s49,    aie_stream& s50,    aie_stream& s51,    aie_stream& s52,    aie_stream& s53,    aie_stream& s54,    aie_stream& s55,    aie_stream& s56,    aie_stream& s57,    aie_stream& s58,    aie_stream& s59,    aie_stream& s60,    aie_stream& s61,    aie_stream& s62,    aie_stream& s63,    aie_stream& s64,    aie_stream& s65,    aie_stream& s66,    aie_stream& s67,    aie_stream& s68,    aie_stream& s69,    aie_stream& s70,    aie_stream& s71,    aie_stream& s72,    aie_stream& s73,    aie_stream& s74,    aie_stream& s75,    aie_stream& s76,    aie_stream& s77,    aie_stream& s78,    aie_stream& s79,    aie_stream& s80,    aie_stream& s81,    aie_stream& s82,    aie_stream& s83,    aie_stream& s84,    aie_stream& s85,    aie_stream& s86,    aie_stream& s87,    aie_stream& s88,    aie_stream& s89,    aie_stream& s90,    aie_stream& s91,    aie_stream& s92,    aie_stream& s93,    aie_stream& s94,    aie_stream& s95) {
#pragma HLS INTERFACE m_axi port=mem offset=slave bundle=gmem depth=65536 \
                                max_write_burst_length=16 num_write_outstanding=16
#pragma HLS INTERFACE s_axilite port=mem bundle=control
#pragma HLS INTERFACE s_axilite port=size bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

#pragma HLS INTERFACE axis port=s0
#pragma HLS INTERFACE axis port=s1
#pragma HLS INTERFACE axis port=s2
#pragma HLS INTERFACE axis port=s3
#pragma HLS INTERFACE axis port=s4
#pragma HLS INTERFACE axis port=s5
#pragma HLS INTERFACE axis port=s6
#pragma HLS INTERFACE axis port=s7
#pragma HLS INTERFACE axis port=s8
#pragma HLS INTERFACE axis port=s9
#pragma HLS INTERFACE axis port=s10
#pragma HLS INTERFACE axis port=s11
#pragma HLS INTERFACE axis port=s12
#pragma HLS INTERFACE axis port=s13
#pragma HLS INTERFACE axis port=s14
#pragma HLS INTERFACE axis port=s15
#pragma HLS INTERFACE axis port=s16
#pragma HLS INTERFACE axis port=s17
#pragma HLS INTERFACE axis port=s18
#pragma HLS INTERFACE axis port=s19
#pragma HLS INTERFACE axis port=s20
#pragma HLS INTERFACE axis port=s21
#pragma HLS INTERFACE axis port=s22
#pragma HLS INTERFACE axis port=s23
#pragma HLS INTERFACE axis port=s24
#pragma HLS INTERFACE axis port=s25
#pragma HLS INTERFACE axis port=s26
#pragma HLS INTERFACE axis port=s27
#pragma HLS INTERFACE axis port=s28
#pragma HLS INTERFACE axis port=s29
#pragma HLS INTERFACE axis port=s30
#pragma HLS INTERFACE axis port=s31
#pragma HLS INTERFACE axis port=s32
#pragma HLS INTERFACE axis port=s33
#pragma HLS INTERFACE axis port=s34
#pragma HLS INTERFACE axis port=s35
#pragma HLS INTERFACE axis port=s36
#pragma HLS INTERFACE axis port=s37
#pragma HLS INTERFACE axis port=s38
#pragma HLS INTERFACE axis port=s39
#pragma HLS INTERFACE axis port=s40
#pragma HLS INTERFACE axis port=s41
#pragma HLS INTERFACE axis port=s42
#pragma HLS INTERFACE axis port=s43
#pragma HLS INTERFACE axis port=s44
#pragma HLS INTERFACE axis port=s45
#pragma HLS INTERFACE axis port=s46
#pragma HLS INTERFACE axis port=s47
#pragma HLS INTERFACE axis port=s48
#pragma HLS INTERFACE axis port=s49
#pragma HLS INTERFACE axis port=s50
#pragma HLS INTERFACE axis port=s51
#pragma HLS INTERFACE axis port=s52
#pragma HLS INTERFACE axis port=s53
#pragma HLS INTERFACE axis port=s54
#pragma HLS INTERFACE axis port=s55
#pragma HLS INTERFACE axis port=s56
#pragma HLS INTERFACE axis port=s57
#pragma HLS INTERFACE axis port=s58
#pragma HLS INTERFACE axis port=s59
#pragma HLS INTERFACE axis port=s60
#pragma HLS INTERFACE axis port=s61
#pragma HLS INTERFACE axis port=s62
#pragma HLS INTERFACE axis port=s63
#pragma HLS INTERFACE axis port=s64
#pragma HLS INTERFACE axis port=s65
#pragma HLS INTERFACE axis port=s66
#pragma HLS INTERFACE axis port=s67
#pragma HLS INTERFACE axis port=s68
#pragma HLS INTERFACE axis port=s69
#pragma HLS INTERFACE axis port=s70
#pragma HLS INTERFACE axis port=s71
#pragma HLS INTERFACE axis port=s72
#pragma HLS INTERFACE axis port=s73
#pragma HLS INTERFACE axis port=s74
#pragma HLS INTERFACE axis port=s75
#pragma HLS INTERFACE axis port=s76
#pragma HLS INTERFACE axis port=s77
#pragma HLS INTERFACE axis port=s78
#pragma HLS INTERFACE axis port=s79
#pragma HLS INTERFACE axis port=s80
#pragma HLS INTERFACE axis port=s81
#pragma HLS INTERFACE axis port=s82
#pragma HLS INTERFACE axis port=s83
#pragma HLS INTERFACE axis port=s84
#pragma HLS INTERFACE axis port=s85
#pragma HLS INTERFACE axis port=s86
#pragma HLS INTERFACE axis port=s87
#pragma HLS INTERFACE axis port=s88
#pragma HLS INTERFACE axis port=s89
#pragma HLS INTERFACE axis port=s90
#pragma HLS INTERFACE axis port=s91
#pragma HLS INTERFACE axis port=s92
#pragma HLS INTERFACE axis port=s93
#pragma HLS INTERFACE axis port=s94
#pragma HLS INTERFACE axis port=s95

DDR_WRITE_LOOP:
    for (unsigned int n = 0; n < size; ++n) {
#pragma HLS PIPELINE II=6

        // Read one 64-bit complex sample from every input stream.
        axis_pkt p0 = s0.read();
        axis_pkt p1 = s1.read();
        axis_pkt p2 = s2.read();
        axis_pkt p3 = s3.read();
        axis_pkt p4 = s4.read();
        axis_pkt p5 = s5.read();
        axis_pkt p6 = s6.read();
        axis_pkt p7 = s7.read();
        axis_pkt p8 = s8.read();
        axis_pkt p9 = s9.read();
        axis_pkt p10 = s10.read();
        axis_pkt p11 = s11.read();
        axis_pkt p12 = s12.read();
        axis_pkt p13 = s13.read();
        axis_pkt p14 = s14.read();
        axis_pkt p15 = s15.read();
        axis_pkt p16 = s16.read();
        axis_pkt p17 = s17.read();
        axis_pkt p18 = s18.read();
        axis_pkt p19 = s19.read();
        axis_pkt p20 = s20.read();
        axis_pkt p21 = s21.read();
        axis_pkt p22 = s22.read();
        axis_pkt p23 = s23.read();
        axis_pkt p24 = s24.read();
        axis_pkt p25 = s25.read();
        axis_pkt p26 = s26.read();
        axis_pkt p27 = s27.read();
        axis_pkt p28 = s28.read();
        axis_pkt p29 = s29.read();
        axis_pkt p30 = s30.read();
        axis_pkt p31 = s31.read();
        axis_pkt p32 = s32.read();
        axis_pkt p33 = s33.read();
        axis_pkt p34 = s34.read();
        axis_pkt p35 = s35.read();
        axis_pkt p36 = s36.read();
        axis_pkt p37 = s37.read();
        axis_pkt p38 = s38.read();
        axis_pkt p39 = s39.read();
        axis_pkt p40 = s40.read();
        axis_pkt p41 = s41.read();
        axis_pkt p42 = s42.read();
        axis_pkt p43 = s43.read();
        axis_pkt p44 = s44.read();
        axis_pkt p45 = s45.read();
        axis_pkt p46 = s46.read();
        axis_pkt p47 = s47.read();
        axis_pkt p48 = s48.read();
        axis_pkt p49 = s49.read();
        axis_pkt p50 = s50.read();
        axis_pkt p51 = s51.read();
        axis_pkt p52 = s52.read();
        axis_pkt p53 = s53.read();
        axis_pkt p54 = s54.read();
        axis_pkt p55 = s55.read();
        axis_pkt p56 = s56.read();
        axis_pkt p57 = s57.read();
        axis_pkt p58 = s58.read();
        axis_pkt p59 = s59.read();
        axis_pkt p60 = s60.read();
        axis_pkt p61 = s61.read();
        axis_pkt p62 = s62.read();
        axis_pkt p63 = s63.read();
        axis_pkt p64 = s64.read();
        axis_pkt p65 = s65.read();
        axis_pkt p66 = s66.read();
        axis_pkt p67 = s67.read();
        axis_pkt p68 = s68.read();
        axis_pkt p69 = s69.read();
        axis_pkt p70 = s70.read();
        axis_pkt p71 = s71.read();
        axis_pkt p72 = s72.read();
        axis_pkt p73 = s73.read();
        axis_pkt p74 = s74.read();
        axis_pkt p75 = s75.read();
        axis_pkt p76 = s76.read();
        axis_pkt p77 = s77.read();
        axis_pkt p78 = s78.read();
        axis_pkt p79 = s79.read();
        axis_pkt p80 = s80.read();
        axis_pkt p81 = s81.read();
        axis_pkt p82 = s82.read();
        axis_pkt p83 = s83.read();
        axis_pkt p84 = s84.read();
        axis_pkt p85 = s85.read();
        axis_pkt p86 = s86.read();
        axis_pkt p87 = s87.read();
        axis_pkt p88 = s88.read();
        axis_pkt p89 = s89.read();
        axis_pkt p90 = s90.read();
        axis_pkt p91 = s91.read();
        axis_pkt p92 = s92.read();
        axis_pkt p93 = s93.read();
        axis_pkt p94 = s94.read();
        axis_pkt p95 = s95.read();

        // Twiddle for this sample:
        //
        //     e^(-j theta) = cos(theta) - j sin(theta)
        //
        // twiddle_re[n] =  cos(theta_n)
        // twiddle_im[n] = -sin(theta_n)
        float c = twiddle_re[n];
        float s = -twiddle_im[n];

        // Unpack each complex packet into real and imaginary floats.
        float re0;
        float im0;
        unpack_complex(p0.data, re0, im0);
        float re1;
        float im1;
        unpack_complex(p1.data, re1, im1);
        float re2;
        float im2;
        unpack_complex(p2.data, re2, im2);
        float re3;
        float im3;
        unpack_complex(p3.data, re3, im3);
        float re4;
        float im4;
        unpack_complex(p4.data, re4, im4);
        float re5;
        float im5;
        unpack_complex(p5.data, re5, im5);
        float re6;
        float im6;
        unpack_complex(p6.data, re6, im6);
        float re7;
        float im7;
        unpack_complex(p7.data, re7, im7);
        float re8;
        float im8;
        unpack_complex(p8.data, re8, im8);
        float re9;
        float im9;
        unpack_complex(p9.data, re9, im9);
        float re10;
        float im10;
        unpack_complex(p10.data, re10, im10);
        float re11;
        float im11;
        unpack_complex(p11.data, re11, im11);
        float re12;
        float im12;
        unpack_complex(p12.data, re12, im12);
        float re13;
        float im13;
        unpack_complex(p13.data, re13, im13);
        float re14;
        float im14;
        unpack_complex(p14.data, re14, im14);
        float re15;
        float im15;
        unpack_complex(p15.data, re15, im15);
        float re16;
        float im16;
        unpack_complex(p16.data, re16, im16);
        float re17;
        float im17;
        unpack_complex(p17.data, re17, im17);
        float re18;
        float im18;
        unpack_complex(p18.data, re18, im18);
        float re19;
        float im19;
        unpack_complex(p19.data, re19, im19);
        float re20;
        float im20;
        unpack_complex(p20.data, re20, im20);
        float re21;
        float im21;
        unpack_complex(p21.data, re21, im21);
        float re22;
        float im22;
        unpack_complex(p22.data, re22, im22);
        float re23;
        float im23;
        unpack_complex(p23.data, re23, im23);
        float re24;
        float im24;
        unpack_complex(p24.data, re24, im24);
        float re25;
        float im25;
        unpack_complex(p25.data, re25, im25);
        float re26;
        float im26;
        unpack_complex(p26.data, re26, im26);
        float re27;
        float im27;
        unpack_complex(p27.data, re27, im27);
        float re28;
        float im28;
        unpack_complex(p28.data, re28, im28);
        float re29;
        float im29;
        unpack_complex(p29.data, re29, im29);
        float re30;
        float im30;
        unpack_complex(p30.data, re30, im30);
        float re31;
        float im31;
        unpack_complex(p31.data, re31, im31);
        float re32;
        float im32;
        unpack_complex(p32.data, re32, im32);
        float re33;
        float im33;
        unpack_complex(p33.data, re33, im33);
        float re34;
        float im34;
        unpack_complex(p34.data, re34, im34);
        float re35;
        float im35;
        unpack_complex(p35.data, re35, im35);
        float re36;
        float im36;
        unpack_complex(p36.data, re36, im36);
        float re37;
        float im37;
        unpack_complex(p37.data, re37, im37);
        float re38;
        float im38;
        unpack_complex(p38.data, re38, im38);
        float re39;
        float im39;
        unpack_complex(p39.data, re39, im39);
        float re40;
        float im40;
        unpack_complex(p40.data, re40, im40);
        float re41;
        float im41;
        unpack_complex(p41.data, re41, im41);
        float re42;
        float im42;
        unpack_complex(p42.data, re42, im42);
        float re43;
        float im43;
        unpack_complex(p43.data, re43, im43);
        float re44;
        float im44;
        unpack_complex(p44.data, re44, im44);
        float re45;
        float im45;
        unpack_complex(p45.data, re45, im45);
        float re46;
        float im46;
        unpack_complex(p46.data, re46, im46);
        float re47;
        float im47;
        unpack_complex(p47.data, re47, im47);
        float re48;
        float im48;
        unpack_complex(p48.data, re48, im48);
        float re49;
        float im49;
        unpack_complex(p49.data, re49, im49);
        float re50;
        float im50;
        unpack_complex(p50.data, re50, im50);
        float re51;
        float im51;
        unpack_complex(p51.data, re51, im51);
        float re52;
        float im52;
        unpack_complex(p52.data, re52, im52);
        float re53;
        float im53;
        unpack_complex(p53.data, re53, im53);
        float re54;
        float im54;
        unpack_complex(p54.data, re54, im54);
        float re55;
        float im55;
        unpack_complex(p55.data, re55, im55);
        float re56;
        float im56;
        unpack_complex(p56.data, re56, im56);
        float re57;
        float im57;
        unpack_complex(p57.data, re57, im57);
        float re58;
        float im58;
        unpack_complex(p58.data, re58, im58);
        float re59;
        float im59;
        unpack_complex(p59.data, re59, im59);
        float re60;
        float im60;
        unpack_complex(p60.data, re60, im60);
        float re61;
        float im61;
        unpack_complex(p61.data, re61, im61);
        float re62;
        float im62;
        unpack_complex(p62.data, re62, im62);
        float re63;
        float im63;
        unpack_complex(p63.data, re63, im63);
        float re64;
        float im64;
        unpack_complex(p64.data, re64, im64);
        float re65;
        float im65;
        unpack_complex(p65.data, re65, im65);
        float re66;
        float im66;
        unpack_complex(p66.data, re66, im66);
        float re67;
        float im67;
        unpack_complex(p67.data, re67, im67);
        float re68;
        float im68;
        unpack_complex(p68.data, re68, im68);
        float re69;
        float im69;
        unpack_complex(p69.data, re69, im69);
        float re70;
        float im70;
        unpack_complex(p70.data, re70, im70);
        float re71;
        float im71;
        unpack_complex(p71.data, re71, im71);
        float re72;
        float im72;
        unpack_complex(p72.data, re72, im72);
        float re73;
        float im73;
        unpack_complex(p73.data, re73, im73);
        float re74;
        float im74;
        unpack_complex(p74.data, re74, im74);
        float re75;
        float im75;
        unpack_complex(p75.data, re75, im75);
        float re76;
        float im76;
        unpack_complex(p76.data, re76, im76);
        float re77;
        float im77;
        unpack_complex(p77.data, re77, im77);
        float re78;
        float im78;
        unpack_complex(p78.data, re78, im78);
        float re79;
        float im79;
        unpack_complex(p79.data, re79, im79);
        float re80;
        float im80;
        unpack_complex(p80.data, re80, im80);
        float re81;
        float im81;
        unpack_complex(p81.data, re81, im81);
        float re82;
        float im82;
        unpack_complex(p82.data, re82, im82);
        float re83;
        float im83;
        unpack_complex(p83.data, re83, im83);
        float re84;
        float im84;
        unpack_complex(p84.data, re84, im84);
        float re85;
        float im85;
        unpack_complex(p85.data, re85, im85);
        float re86;
        float im86;
        unpack_complex(p86.data, re86, im86);
        float re87;
        float im87;
        unpack_complex(p87.data, re87, im87);
        float re88;
        float im88;
        unpack_complex(p88.data, re88, im88);
        float re89;
        float im89;
        unpack_complex(p89.data, re89, im89);
        float re90;
        float im90;
        unpack_complex(p90.data, re90, im90);
        float re91;
        float im91;
        unpack_complex(p91.data, re91, im91);
        float re92;
        float im92;
        unpack_complex(p92.data, re92, im92);
        float re93;
        float im93;
        unpack_complex(p93.data, re93, im93);
        float re94;
        float im94;
        unpack_complex(p94.data, re94, im94);
        float re95;
        float im95;
        unpack_complex(p95.data, re95, im95);

        // Multiply by the twiddle factor:
        //
        // 2 * (re + j*im) * (c - j*s)
        //
        // The real component is:
        //
        // 2 * (re*c + im*s)
        float out_re0 = 2.0f * (re0 * c + im0 * s);
        float out_re1 = 2.0f * (re1 * c + im1 * s);
        float out_re2 = 2.0f * (re2 * c + im2 * s);
        float out_re3 = 2.0f * (re3 * c + im3 * s);
        float out_re4 = 2.0f * (re4 * c + im4 * s);
        float out_re5 = 2.0f * (re5 * c + im5 * s);
        float out_re6 = 2.0f * (re6 * c + im6 * s);
        float out_re7 = 2.0f * (re7 * c + im7 * s);
        float out_re8 = 2.0f * (re8 * c + im8 * s);
        float out_re9 = 2.0f * (re9 * c + im9 * s);
        float out_re10 = 2.0f * (re10 * c + im10 * s);
        float out_re11 = 2.0f * (re11 * c + im11 * s);
        float out_re12 = 2.0f * (re12 * c + im12 * s);
        float out_re13 = 2.0f * (re13 * c + im13 * s);
        float out_re14 = 2.0f * (re14 * c + im14 * s);
        float out_re15 = 2.0f * (re15 * c + im15 * s);
        float out_re16 = 2.0f * (re16 * c + im16 * s);
        float out_re17 = 2.0f * (re17 * c + im17 * s);
        float out_re18 = 2.0f * (re18 * c + im18 * s);
        float out_re19 = 2.0f * (re19 * c + im19 * s);
        float out_re20 = 2.0f * (re20 * c + im20 * s);
        float out_re21 = 2.0f * (re21 * c + im21 * s);
        float out_re22 = 2.0f * (re22 * c + im22 * s);
        float out_re23 = 2.0f * (re23 * c + im23 * s);
        float out_re24 = 2.0f * (re24 * c + im24 * s);
        float out_re25 = 2.0f * (re25 * c + im25 * s);
        float out_re26 = 2.0f * (re26 * c + im26 * s);
        float out_re27 = 2.0f * (re27 * c + im27 * s);
        float out_re28 = 2.0f * (re28 * c + im28 * s);
        float out_re29 = 2.0f * (re29 * c + im29 * s);
        float out_re30 = 2.0f * (re30 * c + im30 * s);
        float out_re31 = 2.0f * (re31 * c + im31 * s);
        float out_re32 = 2.0f * (re32 * c + im32 * s);
        float out_re33 = 2.0f * (re33 * c + im33 * s);
        float out_re34 = 2.0f * (re34 * c + im34 * s);
        float out_re35 = 2.0f * (re35 * c + im35 * s);
        float out_re36 = 2.0f * (re36 * c + im36 * s);
        float out_re37 = 2.0f * (re37 * c + im37 * s);
        float out_re38 = 2.0f * (re38 * c + im38 * s);
        float out_re39 = 2.0f * (re39 * c + im39 * s);
        float out_re40 = 2.0f * (re40 * c + im40 * s);
        float out_re41 = 2.0f * (re41 * c + im41 * s);
        float out_re42 = 2.0f * (re42 * c + im42 * s);
        float out_re43 = 2.0f * (re43 * c + im43 * s);
        float out_re44 = 2.0f * (re44 * c + im44 * s);
        float out_re45 = 2.0f * (re45 * c + im45 * s);
        float out_re46 = 2.0f * (re46 * c + im46 * s);
        float out_re47 = 2.0f * (re47 * c + im47 * s);
        float out_re48 = 2.0f * (re48 * c + im48 * s);
        float out_re49 = 2.0f * (re49 * c + im49 * s);
        float out_re50 = 2.0f * (re50 * c + im50 * s);
        float out_re51 = 2.0f * (re51 * c + im51 * s);
        float out_re52 = 2.0f * (re52 * c + im52 * s);
        float out_re53 = 2.0f * (re53 * c + im53 * s);
        float out_re54 = 2.0f * (re54 * c + im54 * s);
        float out_re55 = 2.0f * (re55 * c + im55 * s);
        float out_re56 = 2.0f * (re56 * c + im56 * s);
        float out_re57 = 2.0f * (re57 * c + im57 * s);
        float out_re58 = 2.0f * (re58 * c + im58 * s);
        float out_re59 = 2.0f * (re59 * c + im59 * s);
        float out_re60 = 2.0f * (re60 * c + im60 * s);
        float out_re61 = 2.0f * (re61 * c + im61 * s);
        float out_re62 = 2.0f * (re62 * c + im62 * s);
        float out_re63 = 2.0f * (re63 * c + im63 * s);
        float out_re64 = 2.0f * (re64 * c + im64 * s);
        float out_re65 = 2.0f * (re65 * c + im65 * s);
        float out_re66 = 2.0f * (re66 * c + im66 * s);
        float out_re67 = 2.0f * (re67 * c + im67 * s);
        float out_re68 = 2.0f * (re68 * c + im68 * s);
        float out_re69 = 2.0f * (re69 * c + im69 * s);
        float out_re70 = 2.0f * (re70 * c + im70 * s);
        float out_re71 = 2.0f * (re71 * c + im71 * s);
        float out_re72 = 2.0f * (re72 * c + im72 * s);
        float out_re73 = 2.0f * (re73 * c + im73 * s);
        float out_re74 = 2.0f * (re74 * c + im74 * s);
        float out_re75 = 2.0f * (re75 * c + im75 * s);
        float out_re76 = 2.0f * (re76 * c + im76 * s);
        float out_re77 = 2.0f * (re77 * c + im77 * s);
        float out_re78 = 2.0f * (re78 * c + im78 * s);
        float out_re79 = 2.0f * (re79 * c + im79 * s);
        float out_re80 = 2.0f * (re80 * c + im80 * s);
        float out_re81 = 2.0f * (re81 * c + im81 * s);
        float out_re82 = 2.0f * (re82 * c + im82 * s);
        float out_re83 = 2.0f * (re83 * c + im83 * s);
        float out_re84 = 2.0f * (re84 * c + im84 * s);
        float out_re85 = 2.0f * (re85 * c + im85 * s);
        float out_re86 = 2.0f * (re86 * c + im86 * s);
        float out_re87 = 2.0f * (re87 * c + im87 * s);
        float out_re88 = 2.0f * (re88 * c + im88 * s);
        float out_re89 = 2.0f * (re89 * c + im89 * s);
        float out_re90 = 2.0f * (re90 * c + im90 * s);
        float out_re91 = 2.0f * (re91 * c + im91 * s);
        float out_re92 = 2.0f * (re92 * c + im92 * s);
        float out_re93 = 2.0f * (re93 * c + im93 * s);
        float out_re94 = 2.0f * (re94 * c + im94 * s);
        float out_re95 = 2.0f * (re95 * c + im95 * s);

        // Pack the 32-bit real results into 512-bit DDR words.
        // Unused fields in the final word are left as zero.
        ddr buf0 = 0;
        buf0.range(31, 0) = float_to_bits(out_re0);
        buf0.range(63, 32) = float_to_bits(out_re1);
        buf0.range(95, 64) = float_to_bits(out_re2);
        buf0.range(127, 96) = float_to_bits(out_re3);
        buf0.range(159, 128) = float_to_bits(out_re4);
        buf0.range(191, 160) = float_to_bits(out_re5);
        buf0.range(223, 192) = float_to_bits(out_re6);
        buf0.range(255, 224) = float_to_bits(out_re7);
        buf0.range(287, 256) = float_to_bits(out_re8);
        buf0.range(319, 288) = float_to_bits(out_re9);
        buf0.range(351, 320) = float_to_bits(out_re10);
        buf0.range(383, 352) = float_to_bits(out_re11);
        buf0.range(415, 384) = float_to_bits(out_re12);
        buf0.range(447, 416) = float_to_bits(out_re13);
        buf0.range(479, 448) = float_to_bits(out_re14);
        buf0.range(511, 480) = float_to_bits(out_re15);
        ddr buf1 = 0;
        buf1.range(31, 0) = float_to_bits(out_re16);
        buf1.range(63, 32) = float_to_bits(out_re17);
        buf1.range(95, 64) = float_to_bits(out_re18);
        buf1.range(127, 96) = float_to_bits(out_re19);
        buf1.range(159, 128) = float_to_bits(out_re20);
        buf1.range(191, 160) = float_to_bits(out_re21);
        buf1.range(223, 192) = float_to_bits(out_re22);
        buf1.range(255, 224) = float_to_bits(out_re23);
        buf1.range(287, 256) = float_to_bits(out_re24);
        buf1.range(319, 288) = float_to_bits(out_re25);
        buf1.range(351, 320) = float_to_bits(out_re26);
        buf1.range(383, 352) = float_to_bits(out_re27);
        buf1.range(415, 384) = float_to_bits(out_re28);
        buf1.range(447, 416) = float_to_bits(out_re29);
        buf1.range(479, 448) = float_to_bits(out_re30);
        buf1.range(511, 480) = float_to_bits(out_re31);
        ddr buf2 = 0;
        buf2.range(31, 0) = float_to_bits(out_re32);
        buf2.range(63, 32) = float_to_bits(out_re33);
        buf2.range(95, 64) = float_to_bits(out_re34);
        buf2.range(127, 96) = float_to_bits(out_re35);
        buf2.range(159, 128) = float_to_bits(out_re36);
        buf2.range(191, 160) = float_to_bits(out_re37);
        buf2.range(223, 192) = float_to_bits(out_re38);
        buf2.range(255, 224) = float_to_bits(out_re39);
        buf2.range(287, 256) = float_to_bits(out_re40);
        buf2.range(319, 288) = float_to_bits(out_re41);
        buf2.range(351, 320) = float_to_bits(out_re42);
        buf2.range(383, 352) = float_to_bits(out_re43);
        buf2.range(415, 384) = float_to_bits(out_re44);
        buf2.range(447, 416) = float_to_bits(out_re45);
        buf2.range(479, 448) = float_to_bits(out_re46);
        buf2.range(511, 480) = float_to_bits(out_re47);
        ddr buf3 = 0;
        buf3.range(31, 0) = float_to_bits(out_re48);
        buf3.range(63, 32) = float_to_bits(out_re49);
        buf3.range(95, 64) = float_to_bits(out_re50);
        buf3.range(127, 96) = float_to_bits(out_re51);
        buf3.range(159, 128) = float_to_bits(out_re52);
        buf3.range(191, 160) = float_to_bits(out_re53);
        buf3.range(223, 192) = float_to_bits(out_re54);
        buf3.range(255, 224) = float_to_bits(out_re55);
        buf3.range(287, 256) = float_to_bits(out_re56);
        buf3.range(319, 288) = float_to_bits(out_re57);
        buf3.range(351, 320) = float_to_bits(out_re58);
        buf3.range(383, 352) = float_to_bits(out_re59);
        buf3.range(415, 384) = float_to_bits(out_re60);
        buf3.range(447, 416) = float_to_bits(out_re61);
        buf3.range(479, 448) = float_to_bits(out_re62);
        buf3.range(511, 480) = float_to_bits(out_re63);
        ddr buf4 = 0;
        buf4.range(31, 0) = float_to_bits(out_re64);
        buf4.range(63, 32) = float_to_bits(out_re65);
        buf4.range(95, 64) = float_to_bits(out_re66);
        buf4.range(127, 96) = float_to_bits(out_re67);
        buf4.range(159, 128) = float_to_bits(out_re68);
        buf4.range(191, 160) = float_to_bits(out_re69);
        buf4.range(223, 192) = float_to_bits(out_re70);
        buf4.range(255, 224) = float_to_bits(out_re71);
        buf4.range(287, 256) = float_to_bits(out_re72);
        buf4.range(319, 288) = float_to_bits(out_re73);
        buf4.range(351, 320) = float_to_bits(out_re74);
        buf4.range(383, 352) = float_to_bits(out_re75);
        buf4.range(415, 384) = float_to_bits(out_re76);
        buf4.range(447, 416) = float_to_bits(out_re77);
        buf4.range(479, 448) = float_to_bits(out_re78);
        buf4.range(511, 480) = float_to_bits(out_re79);
        ddr buf5 = 0;
        buf5.range(31, 0) = float_to_bits(out_re80);
        buf5.range(63, 32) = float_to_bits(out_re81);
        buf5.range(95, 64) = float_to_bits(out_re82);
        buf5.range(127, 96) = float_to_bits(out_re83);
        buf5.range(159, 128) = float_to_bits(out_re84);
        buf5.range(191, 160) = float_to_bits(out_re85);
        buf5.range(223, 192) = float_to_bits(out_re86);
        buf5.range(255, 224) = float_to_bits(out_re87);
        buf5.range(287, 256) = float_to_bits(out_re88);
        buf5.range(319, 288) = float_to_bits(out_re89);
        buf5.range(351, 320) = float_to_bits(out_re90);
        buf5.range(383, 352) = float_to_bits(out_re91);
        buf5.range(415, 384) = float_to_bits(out_re92);
        buf5.range(447, 416) = float_to_bits(out_re93);
        buf5.range(479, 448) = float_to_bits(out_re94);
        buf5.range(511, 480) = float_to_bits(out_re95);

        // Write all packed words for this iteration to consecutive
        // DDR locations.
        const unsigned int base_addr = n * DDR_WORDS_PER_OUTPUT;

        mem[base_addr + 0] = buf0;
        mem[base_addr + 1] = buf1;
        mem[base_addr + 2] = buf2;
        mem[base_addr + 3] = buf3;
        mem[base_addr + 4] = buf4;
        mem[base_addr + 5] = buf5;
    }
}

} // extern "C"
