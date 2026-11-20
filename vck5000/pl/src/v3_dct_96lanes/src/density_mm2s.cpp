#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

extern "C" {

typedef ap_uint<512> ddr;            // 512-bit DDR word
typedef ap_axiu<64, 0, 0, 0> axis_pkt;
typedef hls::stream<axis_pkt> aie_stream;

static constexpr unsigned int NUM_LANES = 96;
static constexpr unsigned int REALS_PER_DDR_WORD = 16;
static constexpr unsigned int DDR_WORDS_PER_OUTPUT =
    (NUM_LANES + REALS_PER_DDR_WORD - 1) / REALS_PER_DDR_WORD;

// PL kernel: read 32-bit real values from DDR and send one 64-bit complex
// packet to each lane. The upper 32 bits, representing the imaginary part,
// are set to zero.
//
// "size" is the number of output packets produced on each lane.
//
// Required DDR storage:
//     size * DDR_WORDS_PER_OUTPUT 512-bit words
//
// Values for output iteration n are stored in a block of
// DDR_WORDS_PER_OUTPUT consecutive DDR words.
void density_mm2s(
    const ddr* mem,
    const unsigned int size,
    aie_stream& s0,    aie_stream& s1,    aie_stream& s2,    aie_stream& s3,    aie_stream& s4,    aie_stream& s5,    aie_stream& s6,    aie_stream& s7,    aie_stream& s8,    aie_stream& s9,    aie_stream& s10,    aie_stream& s11,    aie_stream& s12,    aie_stream& s13,    aie_stream& s14,    aie_stream& s15,    aie_stream& s16,    aie_stream& s17,    aie_stream& s18,    aie_stream& s19,    aie_stream& s20,    aie_stream& s21,    aie_stream& s22,    aie_stream& s23,    aie_stream& s24,    aie_stream& s25,    aie_stream& s26,    aie_stream& s27,    aie_stream& s28,    aie_stream& s29,    aie_stream& s30,    aie_stream& s31,    aie_stream& s32,    aie_stream& s33,    aie_stream& s34,    aie_stream& s35,    aie_stream& s36,    aie_stream& s37,    aie_stream& s38,    aie_stream& s39,    aie_stream& s40,    aie_stream& s41,    aie_stream& s42,    aie_stream& s43,    aie_stream& s44,    aie_stream& s45,    aie_stream& s46,    aie_stream& s47,    aie_stream& s48,    aie_stream& s49,    aie_stream& s50,    aie_stream& s51,    aie_stream& s52,    aie_stream& s53,    aie_stream& s54,    aie_stream& s55,    aie_stream& s56,    aie_stream& s57,    aie_stream& s58,    aie_stream& s59,    aie_stream& s60,    aie_stream& s61,    aie_stream& s62,    aie_stream& s63,    aie_stream& s64,    aie_stream& s65,    aie_stream& s66,    aie_stream& s67,    aie_stream& s68,    aie_stream& s69,    aie_stream& s70,    aie_stream& s71,    aie_stream& s72,    aie_stream& s73,    aie_stream& s74,    aie_stream& s75,    aie_stream& s76,    aie_stream& s77,    aie_stream& s78,    aie_stream& s79,    aie_stream& s80,    aie_stream& s81,    aie_stream& s82,    aie_stream& s83,    aie_stream& s84,    aie_stream& s85,    aie_stream& s86,    aie_stream& s87,    aie_stream& s88,    aie_stream& s89,    aie_stream& s90,    aie_stream& s91,    aie_stream& s92,    aie_stream& s93,    aie_stream& s94,    aie_stream& s95) {
#pragma HLS INTERFACE m_axi port=mem offset=slave bundle=gmem depth=65536 \
                                max_read_burst_length=16 num_read_outstanding=16
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

DDR_READ_LOOP:
    for (unsigned int n = 0; n < size; ++n) {
#pragma HLS PIPELINE II=6

        // Preserve the address ordering from the original kernel:
        //
        //   logical_addr = 2*n                    when n < size/2
        //   logical_addr = 2*size - 1 - 2*n       otherwise
        //
        // Each logical address now identifies a block containing enough
        // 512-bit words to provide data for all lanes.
        unsigned int logical_addr;

        if (n < (size >> 1)) {
            logical_addr = 2 * n;
        } else {
            logical_addr = 2 * size - 1 - 2 * n;
        }

        const unsigned int base_addr =
            logical_addr * DDR_WORDS_PER_OUTPUT;

        ddr wide_words[DDR_WORDS_PER_OUTPUT];
#pragma HLS ARRAY_PARTITION variable=wide_words complete

        wide_words[0] = mem[base_addr + 0];
        wide_words[1] = mem[base_addr + 1];
        wide_words[2] = mem[base_addr + 2];
        wide_words[3] = mem[base_addr + 3];
        wide_words[4] = mem[base_addr + 4];
        wide_words[5] = mem[base_addr + 5];

        axis_pkt v[NUM_LANES];
#pragma HLS ARRAY_PARTITION variable=v complete

        // Extract one 32-bit real value for each lane and construct
        // a 64-bit complex packet with an imaginary value of zero.
        v[0].data.range(31, 0) =
            wide_words[0].range(
                31,
                0
            );
        v[0].data.range(63, 32) = 0;
        v[0].keep = -1;
        v[0].strb = -1;
        v[0].last = (n == size - 1);
        v[1].data.range(31, 0) =
            wide_words[0].range(
                63,
                32
            );
        v[1].data.range(63, 32) = 0;
        v[1].keep = -1;
        v[1].strb = -1;
        v[1].last = (n == size - 1);
        v[2].data.range(31, 0) =
            wide_words[0].range(
                95,
                64
            );
        v[2].data.range(63, 32) = 0;
        v[2].keep = -1;
        v[2].strb = -1;
        v[2].last = (n == size - 1);
        v[3].data.range(31, 0) =
            wide_words[0].range(
                127,
                96
            );
        v[3].data.range(63, 32) = 0;
        v[3].keep = -1;
        v[3].strb = -1;
        v[3].last = (n == size - 1);
        v[4].data.range(31, 0) =
            wide_words[0].range(
                159,
                128
            );
        v[4].data.range(63, 32) = 0;
        v[4].keep = -1;
        v[4].strb = -1;
        v[4].last = (n == size - 1);
        v[5].data.range(31, 0) =
            wide_words[0].range(
                191,
                160
            );
        v[5].data.range(63, 32) = 0;
        v[5].keep = -1;
        v[5].strb = -1;
        v[5].last = (n == size - 1);
        v[6].data.range(31, 0) =
            wide_words[0].range(
                223,
                192
            );
        v[6].data.range(63, 32) = 0;
        v[6].keep = -1;
        v[6].strb = -1;
        v[6].last = (n == size - 1);
        v[7].data.range(31, 0) =
            wide_words[0].range(
                255,
                224
            );
        v[7].data.range(63, 32) = 0;
        v[7].keep = -1;
        v[7].strb = -1;
        v[7].last = (n == size - 1);
        v[8].data.range(31, 0) =
            wide_words[0].range(
                287,
                256
            );
        v[8].data.range(63, 32) = 0;
        v[8].keep = -1;
        v[8].strb = -1;
        v[8].last = (n == size - 1);
        v[9].data.range(31, 0) =
            wide_words[0].range(
                319,
                288
            );
        v[9].data.range(63, 32) = 0;
        v[9].keep = -1;
        v[9].strb = -1;
        v[9].last = (n == size - 1);
        v[10].data.range(31, 0) =
            wide_words[0].range(
                351,
                320
            );
        v[10].data.range(63, 32) = 0;
        v[10].keep = -1;
        v[10].strb = -1;
        v[10].last = (n == size - 1);
        v[11].data.range(31, 0) =
            wide_words[0].range(
                383,
                352
            );
        v[11].data.range(63, 32) = 0;
        v[11].keep = -1;
        v[11].strb = -1;
        v[11].last = (n == size - 1);
        v[12].data.range(31, 0) =
            wide_words[0].range(
                415,
                384
            );
        v[12].data.range(63, 32) = 0;
        v[12].keep = -1;
        v[12].strb = -1;
        v[12].last = (n == size - 1);
        v[13].data.range(31, 0) =
            wide_words[0].range(
                447,
                416
            );
        v[13].data.range(63, 32) = 0;
        v[13].keep = -1;
        v[13].strb = -1;
        v[13].last = (n == size - 1);
        v[14].data.range(31, 0) =
            wide_words[0].range(
                479,
                448
            );
        v[14].data.range(63, 32) = 0;
        v[14].keep = -1;
        v[14].strb = -1;
        v[14].last = (n == size - 1);
        v[15].data.range(31, 0) =
            wide_words[0].range(
                511,
                480
            );
        v[15].data.range(63, 32) = 0;
        v[15].keep = -1;
        v[15].strb = -1;
        v[15].last = (n == size - 1);
        v[16].data.range(31, 0) =
            wide_words[1].range(
                31,
                0
            );
        v[16].data.range(63, 32) = 0;
        v[16].keep = -1;
        v[16].strb = -1;
        v[16].last = (n == size - 1);
        v[17].data.range(31, 0) =
            wide_words[1].range(
                63,
                32
            );
        v[17].data.range(63, 32) = 0;
        v[17].keep = -1;
        v[17].strb = -1;
        v[17].last = (n == size - 1);
        v[18].data.range(31, 0) =
            wide_words[1].range(
                95,
                64
            );
        v[18].data.range(63, 32) = 0;
        v[18].keep = -1;
        v[18].strb = -1;
        v[18].last = (n == size - 1);
        v[19].data.range(31, 0) =
            wide_words[1].range(
                127,
                96
            );
        v[19].data.range(63, 32) = 0;
        v[19].keep = -1;
        v[19].strb = -1;
        v[19].last = (n == size - 1);
        v[20].data.range(31, 0) =
            wide_words[1].range(
                159,
                128
            );
        v[20].data.range(63, 32) = 0;
        v[20].keep = -1;
        v[20].strb = -1;
        v[20].last = (n == size - 1);
        v[21].data.range(31, 0) =
            wide_words[1].range(
                191,
                160
            );
        v[21].data.range(63, 32) = 0;
        v[21].keep = -1;
        v[21].strb = -1;
        v[21].last = (n == size - 1);
        v[22].data.range(31, 0) =
            wide_words[1].range(
                223,
                192
            );
        v[22].data.range(63, 32) = 0;
        v[22].keep = -1;
        v[22].strb = -1;
        v[22].last = (n == size - 1);
        v[23].data.range(31, 0) =
            wide_words[1].range(
                255,
                224
            );
        v[23].data.range(63, 32) = 0;
        v[23].keep = -1;
        v[23].strb = -1;
        v[23].last = (n == size - 1);
        v[24].data.range(31, 0) =
            wide_words[1].range(
                287,
                256
            );
        v[24].data.range(63, 32) = 0;
        v[24].keep = -1;
        v[24].strb = -1;
        v[24].last = (n == size - 1);
        v[25].data.range(31, 0) =
            wide_words[1].range(
                319,
                288
            );
        v[25].data.range(63, 32) = 0;
        v[25].keep = -1;
        v[25].strb = -1;
        v[25].last = (n == size - 1);
        v[26].data.range(31, 0) =
            wide_words[1].range(
                351,
                320
            );
        v[26].data.range(63, 32) = 0;
        v[26].keep = -1;
        v[26].strb = -1;
        v[26].last = (n == size - 1);
        v[27].data.range(31, 0) =
            wide_words[1].range(
                383,
                352
            );
        v[27].data.range(63, 32) = 0;
        v[27].keep = -1;
        v[27].strb = -1;
        v[27].last = (n == size - 1);
        v[28].data.range(31, 0) =
            wide_words[1].range(
                415,
                384
            );
        v[28].data.range(63, 32) = 0;
        v[28].keep = -1;
        v[28].strb = -1;
        v[28].last = (n == size - 1);
        v[29].data.range(31, 0) =
            wide_words[1].range(
                447,
                416
            );
        v[29].data.range(63, 32) = 0;
        v[29].keep = -1;
        v[29].strb = -1;
        v[29].last = (n == size - 1);
        v[30].data.range(31, 0) =
            wide_words[1].range(
                479,
                448
            );
        v[30].data.range(63, 32) = 0;
        v[30].keep = -1;
        v[30].strb = -1;
        v[30].last = (n == size - 1);
        v[31].data.range(31, 0) =
            wide_words[1].range(
                511,
                480
            );
        v[31].data.range(63, 32) = 0;
        v[31].keep = -1;
        v[31].strb = -1;
        v[31].last = (n == size - 1);
        v[32].data.range(31, 0) =
            wide_words[2].range(
                31,
                0
            );
        v[32].data.range(63, 32) = 0;
        v[32].keep = -1;
        v[32].strb = -1;
        v[32].last = (n == size - 1);
        v[33].data.range(31, 0) =
            wide_words[2].range(
                63,
                32
            );
        v[33].data.range(63, 32) = 0;
        v[33].keep = -1;
        v[33].strb = -1;
        v[33].last = (n == size - 1);
        v[34].data.range(31, 0) =
            wide_words[2].range(
                95,
                64
            );
        v[34].data.range(63, 32) = 0;
        v[34].keep = -1;
        v[34].strb = -1;
        v[34].last = (n == size - 1);
        v[35].data.range(31, 0) =
            wide_words[2].range(
                127,
                96
            );
        v[35].data.range(63, 32) = 0;
        v[35].keep = -1;
        v[35].strb = -1;
        v[35].last = (n == size - 1);
        v[36].data.range(31, 0) =
            wide_words[2].range(
                159,
                128
            );
        v[36].data.range(63, 32) = 0;
        v[36].keep = -1;
        v[36].strb = -1;
        v[36].last = (n == size - 1);
        v[37].data.range(31, 0) =
            wide_words[2].range(
                191,
                160
            );
        v[37].data.range(63, 32) = 0;
        v[37].keep = -1;
        v[37].strb = -1;
        v[37].last = (n == size - 1);
        v[38].data.range(31, 0) =
            wide_words[2].range(
                223,
                192
            );
        v[38].data.range(63, 32) = 0;
        v[38].keep = -1;
        v[38].strb = -1;
        v[38].last = (n == size - 1);
        v[39].data.range(31, 0) =
            wide_words[2].range(
                255,
                224
            );
        v[39].data.range(63, 32) = 0;
        v[39].keep = -1;
        v[39].strb = -1;
        v[39].last = (n == size - 1);
        v[40].data.range(31, 0) =
            wide_words[2].range(
                287,
                256
            );
        v[40].data.range(63, 32) = 0;
        v[40].keep = -1;
        v[40].strb = -1;
        v[40].last = (n == size - 1);
        v[41].data.range(31, 0) =
            wide_words[2].range(
                319,
                288
            );
        v[41].data.range(63, 32) = 0;
        v[41].keep = -1;
        v[41].strb = -1;
        v[41].last = (n == size - 1);
        v[42].data.range(31, 0) =
            wide_words[2].range(
                351,
                320
            );
        v[42].data.range(63, 32) = 0;
        v[42].keep = -1;
        v[42].strb = -1;
        v[42].last = (n == size - 1);
        v[43].data.range(31, 0) =
            wide_words[2].range(
                383,
                352
            );
        v[43].data.range(63, 32) = 0;
        v[43].keep = -1;
        v[43].strb = -1;
        v[43].last = (n == size - 1);
        v[44].data.range(31, 0) =
            wide_words[2].range(
                415,
                384
            );
        v[44].data.range(63, 32) = 0;
        v[44].keep = -1;
        v[44].strb = -1;
        v[44].last = (n == size - 1);
        v[45].data.range(31, 0) =
            wide_words[2].range(
                447,
                416
            );
        v[45].data.range(63, 32) = 0;
        v[45].keep = -1;
        v[45].strb = -1;
        v[45].last = (n == size - 1);
        v[46].data.range(31, 0) =
            wide_words[2].range(
                479,
                448
            );
        v[46].data.range(63, 32) = 0;
        v[46].keep = -1;
        v[46].strb = -1;
        v[46].last = (n == size - 1);
        v[47].data.range(31, 0) =
            wide_words[2].range(
                511,
                480
            );
        v[47].data.range(63, 32) = 0;
        v[47].keep = -1;
        v[47].strb = -1;
        v[47].last = (n == size - 1);
        v[48].data.range(31, 0) =
            wide_words[3].range(
                31,
                0
            );
        v[48].data.range(63, 32) = 0;
        v[48].keep = -1;
        v[48].strb = -1;
        v[48].last = (n == size - 1);
        v[49].data.range(31, 0) =
            wide_words[3].range(
                63,
                32
            );
        v[49].data.range(63, 32) = 0;
        v[49].keep = -1;
        v[49].strb = -1;
        v[49].last = (n == size - 1);
        v[50].data.range(31, 0) =
            wide_words[3].range(
                95,
                64
            );
        v[50].data.range(63, 32) = 0;
        v[50].keep = -1;
        v[50].strb = -1;
        v[50].last = (n == size - 1);
        v[51].data.range(31, 0) =
            wide_words[3].range(
                127,
                96
            );
        v[51].data.range(63, 32) = 0;
        v[51].keep = -1;
        v[51].strb = -1;
        v[51].last = (n == size - 1);
        v[52].data.range(31, 0) =
            wide_words[3].range(
                159,
                128
            );
        v[52].data.range(63, 32) = 0;
        v[52].keep = -1;
        v[52].strb = -1;
        v[52].last = (n == size - 1);
        v[53].data.range(31, 0) =
            wide_words[3].range(
                191,
                160
            );
        v[53].data.range(63, 32) = 0;
        v[53].keep = -1;
        v[53].strb = -1;
        v[53].last = (n == size - 1);
        v[54].data.range(31, 0) =
            wide_words[3].range(
                223,
                192
            );
        v[54].data.range(63, 32) = 0;
        v[54].keep = -1;
        v[54].strb = -1;
        v[54].last = (n == size - 1);
        v[55].data.range(31, 0) =
            wide_words[3].range(
                255,
                224
            );
        v[55].data.range(63, 32) = 0;
        v[55].keep = -1;
        v[55].strb = -1;
        v[55].last = (n == size - 1);
        v[56].data.range(31, 0) =
            wide_words[3].range(
                287,
                256
            );
        v[56].data.range(63, 32) = 0;
        v[56].keep = -1;
        v[56].strb = -1;
        v[56].last = (n == size - 1);
        v[57].data.range(31, 0) =
            wide_words[3].range(
                319,
                288
            );
        v[57].data.range(63, 32) = 0;
        v[57].keep = -1;
        v[57].strb = -1;
        v[57].last = (n == size - 1);
        v[58].data.range(31, 0) =
            wide_words[3].range(
                351,
                320
            );
        v[58].data.range(63, 32) = 0;
        v[58].keep = -1;
        v[58].strb = -1;
        v[58].last = (n == size - 1);
        v[59].data.range(31, 0) =
            wide_words[3].range(
                383,
                352
            );
        v[59].data.range(63, 32) = 0;
        v[59].keep = -1;
        v[59].strb = -1;
        v[59].last = (n == size - 1);
        v[60].data.range(31, 0) =
            wide_words[3].range(
                415,
                384
            );
        v[60].data.range(63, 32) = 0;
        v[60].keep = -1;
        v[60].strb = -1;
        v[60].last = (n == size - 1);
        v[61].data.range(31, 0) =
            wide_words[3].range(
                447,
                416
            );
        v[61].data.range(63, 32) = 0;
        v[61].keep = -1;
        v[61].strb = -1;
        v[61].last = (n == size - 1);
        v[62].data.range(31, 0) =
            wide_words[3].range(
                479,
                448
            );
        v[62].data.range(63, 32) = 0;
        v[62].keep = -1;
        v[62].strb = -1;
        v[62].last = (n == size - 1);
        v[63].data.range(31, 0) =
            wide_words[3].range(
                511,
                480
            );
        v[63].data.range(63, 32) = 0;
        v[63].keep = -1;
        v[63].strb = -1;
        v[63].last = (n == size - 1);
        v[64].data.range(31, 0) =
            wide_words[4].range(
                31,
                0
            );
        v[64].data.range(63, 32) = 0;
        v[64].keep = -1;
        v[64].strb = -1;
        v[64].last = (n == size - 1);
        v[65].data.range(31, 0) =
            wide_words[4].range(
                63,
                32
            );
        v[65].data.range(63, 32) = 0;
        v[65].keep = -1;
        v[65].strb = -1;
        v[65].last = (n == size - 1);
        v[66].data.range(31, 0) =
            wide_words[4].range(
                95,
                64
            );
        v[66].data.range(63, 32) = 0;
        v[66].keep = -1;
        v[66].strb = -1;
        v[66].last = (n == size - 1);
        v[67].data.range(31, 0) =
            wide_words[4].range(
                127,
                96
            );
        v[67].data.range(63, 32) = 0;
        v[67].keep = -1;
        v[67].strb = -1;
        v[67].last = (n == size - 1);
        v[68].data.range(31, 0) =
            wide_words[4].range(
                159,
                128
            );
        v[68].data.range(63, 32) = 0;
        v[68].keep = -1;
        v[68].strb = -1;
        v[68].last = (n == size - 1);
        v[69].data.range(31, 0) =
            wide_words[4].range(
                191,
                160
            );
        v[69].data.range(63, 32) = 0;
        v[69].keep = -1;
        v[69].strb = -1;
        v[69].last = (n == size - 1);
        v[70].data.range(31, 0) =
            wide_words[4].range(
                223,
                192
            );
        v[70].data.range(63, 32) = 0;
        v[70].keep = -1;
        v[70].strb = -1;
        v[70].last = (n == size - 1);
        v[71].data.range(31, 0) =
            wide_words[4].range(
                255,
                224
            );
        v[71].data.range(63, 32) = 0;
        v[71].keep = -1;
        v[71].strb = -1;
        v[71].last = (n == size - 1);
        v[72].data.range(31, 0) =
            wide_words[4].range(
                287,
                256
            );
        v[72].data.range(63, 32) = 0;
        v[72].keep = -1;
        v[72].strb = -1;
        v[72].last = (n == size - 1);
        v[73].data.range(31, 0) =
            wide_words[4].range(
                319,
                288
            );
        v[73].data.range(63, 32) = 0;
        v[73].keep = -1;
        v[73].strb = -1;
        v[73].last = (n == size - 1);
        v[74].data.range(31, 0) =
            wide_words[4].range(
                351,
                320
            );
        v[74].data.range(63, 32) = 0;
        v[74].keep = -1;
        v[74].strb = -1;
        v[74].last = (n == size - 1);
        v[75].data.range(31, 0) =
            wide_words[4].range(
                383,
                352
            );
        v[75].data.range(63, 32) = 0;
        v[75].keep = -1;
        v[75].strb = -1;
        v[75].last = (n == size - 1);
        v[76].data.range(31, 0) =
            wide_words[4].range(
                415,
                384
            );
        v[76].data.range(63, 32) = 0;
        v[76].keep = -1;
        v[76].strb = -1;
        v[76].last = (n == size - 1);
        v[77].data.range(31, 0) =
            wide_words[4].range(
                447,
                416
            );
        v[77].data.range(63, 32) = 0;
        v[77].keep = -1;
        v[77].strb = -1;
        v[77].last = (n == size - 1);
        v[78].data.range(31, 0) =
            wide_words[4].range(
                479,
                448
            );
        v[78].data.range(63, 32) = 0;
        v[78].keep = -1;
        v[78].strb = -1;
        v[78].last = (n == size - 1);
        v[79].data.range(31, 0) =
            wide_words[4].range(
                511,
                480
            );
        v[79].data.range(63, 32) = 0;
        v[79].keep = -1;
        v[79].strb = -1;
        v[79].last = (n == size - 1);
        v[80].data.range(31, 0) =
            wide_words[5].range(
                31,
                0
            );
        v[80].data.range(63, 32) = 0;
        v[80].keep = -1;
        v[80].strb = -1;
        v[80].last = (n == size - 1);
        v[81].data.range(31, 0) =
            wide_words[5].range(
                63,
                32
            );
        v[81].data.range(63, 32) = 0;
        v[81].keep = -1;
        v[81].strb = -1;
        v[81].last = (n == size - 1);
        v[82].data.range(31, 0) =
            wide_words[5].range(
                95,
                64
            );
        v[82].data.range(63, 32) = 0;
        v[82].keep = -1;
        v[82].strb = -1;
        v[82].last = (n == size - 1);
        v[83].data.range(31, 0) =
            wide_words[5].range(
                127,
                96
            );
        v[83].data.range(63, 32) = 0;
        v[83].keep = -1;
        v[83].strb = -1;
        v[83].last = (n == size - 1);
        v[84].data.range(31, 0) =
            wide_words[5].range(
                159,
                128
            );
        v[84].data.range(63, 32) = 0;
        v[84].keep = -1;
        v[84].strb = -1;
        v[84].last = (n == size - 1);
        v[85].data.range(31, 0) =
            wide_words[5].range(
                191,
                160
            );
        v[85].data.range(63, 32) = 0;
        v[85].keep = -1;
        v[85].strb = -1;
        v[85].last = (n == size - 1);
        v[86].data.range(31, 0) =
            wide_words[5].range(
                223,
                192
            );
        v[86].data.range(63, 32) = 0;
        v[86].keep = -1;
        v[86].strb = -1;
        v[86].last = (n == size - 1);
        v[87].data.range(31, 0) =
            wide_words[5].range(
                255,
                224
            );
        v[87].data.range(63, 32) = 0;
        v[87].keep = -1;
        v[87].strb = -1;
        v[87].last = (n == size - 1);
        v[88].data.range(31, 0) =
            wide_words[5].range(
                287,
                256
            );
        v[88].data.range(63, 32) = 0;
        v[88].keep = -1;
        v[88].strb = -1;
        v[88].last = (n == size - 1);
        v[89].data.range(31, 0) =
            wide_words[5].range(
                319,
                288
            );
        v[89].data.range(63, 32) = 0;
        v[89].keep = -1;
        v[89].strb = -1;
        v[89].last = (n == size - 1);
        v[90].data.range(31, 0) =
            wide_words[5].range(
                351,
                320
            );
        v[90].data.range(63, 32) = 0;
        v[90].keep = -1;
        v[90].strb = -1;
        v[90].last = (n == size - 1);
        v[91].data.range(31, 0) =
            wide_words[5].range(
                383,
                352
            );
        v[91].data.range(63, 32) = 0;
        v[91].keep = -1;
        v[91].strb = -1;
        v[91].last = (n == size - 1);
        v[92].data.range(31, 0) =
            wide_words[5].range(
                415,
                384
            );
        v[92].data.range(63, 32) = 0;
        v[92].keep = -1;
        v[92].strb = -1;
        v[92].last = (n == size - 1);
        v[93].data.range(31, 0) =
            wide_words[5].range(
                447,
                416
            );
        v[93].data.range(63, 32) = 0;
        v[93].keep = -1;
        v[93].strb = -1;
        v[93].last = (n == size - 1);
        v[94].data.range(31, 0) =
            wide_words[5].range(
                479,
                448
            );
        v[94].data.range(63, 32) = 0;
        v[94].keep = -1;
        v[94].strb = -1;
        v[94].last = (n == size - 1);
        v[95].data.range(31, 0) =
            wide_words[5].range(
                511,
                480
            );
        v[95].data.range(63, 32) = 0;
        v[95].keep = -1;
        v[95].strb = -1;
        v[95].last = (n == size - 1);

        // Send each value to its corresponding AXI stream.
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
        s16.write(v[16]);
        s17.write(v[17]);
        s18.write(v[18]);
        s19.write(v[19]);
        s20.write(v[20]);
        s21.write(v[21]);
        s22.write(v[22]);
        s23.write(v[23]);
        s24.write(v[24]);
        s25.write(v[25]);
        s26.write(v[26]);
        s27.write(v[27]);
        s28.write(v[28]);
        s29.write(v[29]);
        s30.write(v[30]);
        s31.write(v[31]);
        s32.write(v[32]);
        s33.write(v[33]);
        s34.write(v[34]);
        s35.write(v[35]);
        s36.write(v[36]);
        s37.write(v[37]);
        s38.write(v[38]);
        s39.write(v[39]);
        s40.write(v[40]);
        s41.write(v[41]);
        s42.write(v[42]);
        s43.write(v[43]);
        s44.write(v[44]);
        s45.write(v[45]);
        s46.write(v[46]);
        s47.write(v[47]);
        s48.write(v[48]);
        s49.write(v[49]);
        s50.write(v[50]);
        s51.write(v[51]);
        s52.write(v[52]);
        s53.write(v[53]);
        s54.write(v[54]);
        s55.write(v[55]);
        s56.write(v[56]);
        s57.write(v[57]);
        s58.write(v[58]);
        s59.write(v[59]);
        s60.write(v[60]);
        s61.write(v[61]);
        s62.write(v[62]);
        s63.write(v[63]);
        s64.write(v[64]);
        s65.write(v[65]);
        s66.write(v[66]);
        s67.write(v[67]);
        s68.write(v[68]);
        s69.write(v[69]);
        s70.write(v[70]);
        s71.write(v[71]);
        s72.write(v[72]);
        s73.write(v[73]);
        s74.write(v[74]);
        s75.write(v[75]);
        s76.write(v[76]);
        s77.write(v[77]);
        s78.write(v[78]);
        s79.write(v[79]);
        s80.write(v[80]);
        s81.write(v[81]);
        s82.write(v[82]);
        s83.write(v[83]);
        s84.write(v[84]);
        s85.write(v[85]);
        s86.write(v[86]);
        s87.write(v[87]);
        s88.write(v[88]);
        s89.write(v[89]);
        s90.write(v[90]);
        s91.write(v[91]);
        s92.write(v[92]);
        s93.write(v[93]);
        s94.write(v[94]);
        s95.write(v[95]);
    }
}

} // extern "C"
