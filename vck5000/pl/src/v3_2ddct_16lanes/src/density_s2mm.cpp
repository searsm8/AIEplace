#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>
#include <cstdint>

#include "twiddle_table.h"

extern "C"  {

typedef ap_uint<512> ddr;                  // 512-bit DDR word
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

// PL kernel: receive 64-bit words from AXIS and write to DDR as 512-bit beats
// 'size' is the number of 64-bit words expected from the stream. Currently ignored and fixed to 1024. Remove in the future
void density_s2mm(ddr* mem, const unsigned int size,
		aie_stream& s1,aie_stream& s2,
		aie_stream& s3,aie_stream& s4,
		aie_stream& s5,aie_stream& s6,
		aie_stream& s7,aie_stream& s8,
                aie_stream& s9,  aie_stream& s10,
                aie_stream& s11, aie_stream& s12,
                aie_stream& s13, aie_stream& s14,
                aie_stream& s15, aie_stream& s16
		) {
#pragma HLS INTERFACE m_axi     port=mem offset=slave bundle=gmem depth=1024 \
                                max_write_burst_length=16 num_write_outstanding=16
#pragma HLS INTERFACE s_axilite port=mem    bundle=control
#pragma HLS INTERFACE s_axilite port=size   bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

#pragma HLS INTERFACE axis      port=s1
#pragma HLS INTERFACE axis      port=s2
#pragma HLS INTERFACE axis      port=s3
#pragma HLS INTERFACE axis      port=s4
#pragma HLS INTERFACE axis      port=s5
#pragma HLS INTERFACE axis      port=s6
#pragma HLS INTERFACE axis      port=s7
#pragma HLS INTERFACE axis      port=s8
#pragma HLS INTERFACE axis      port=s9
#pragma HLS INTERFACE axis      port=s10
#pragma HLS INTERFACE axis      port=s11
#pragma HLS INTERFACE axis      port=s12
#pragma HLS INTERFACE axis      port=s13
#pragma HLS INTERFACE axis      port=s14
#pragma HLS INTERFACE axis      port=s15
#pragma HLS INTERFACE axis      port=s16

DDR_WRITE_LOOP:
    for (unsigned int n = 0; n < size; ++n) {
    #pragma HLS PIPELINE II=1

        // Read one complex sample (64-bit) from each stream
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

        // Twiddle for this beat: e^{-jθ} = cosθ - j sinθ
        // twiddle_re[n] = cosθ_n, twiddle_im[n] = -sinθ_n
        float c = twiddle_re[n];   // cos(θ_n)
        float s = -twiddle_im[n];  // sin(θ_n)

        // Unpack into complex floats
        float re1, im1, re2, im2, re3, im3, re4, im4;
        float re5, im5, re6, im6, re7, im7, re8, im8;
        float re9, im9, re10, im10, re11, im11, re12, im12;
        float re13, im13, re14, im14, re15, im15, re16, im16;

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
        unpack_complex(p16.data, re16, im16);

        // Multiply each by e^{-jθ}:
        // 2 * (re + j im) * (c - j s) = 2 * ((re*c + im*s) + j(im*c - re*s))
	// We only need the real part for DCT, so only calculate 2 * (re*c + im*s)
        float out_re1  = 2.0f * (re1*c + im1*s);
        float out_re2  = 2.0f * (re2*c + im2*s);
        float out_re3  = 2.0f * (re3*c + im3*s);
        float out_re4  = 2.0f * (re4*c + im4*s);
        float out_re5  = 2.0f * (re5*c + im5*s);
        float out_re6  = 2.0f * (re6*c + im6*s);
        float out_re7  = 2.0f * (re7*c + im7*s);
        float out_re8  = 2.0f * (re8*c + im8*s);
	float out_re9  = 2.0f * (re9*c  + im9*s);
        float out_re10 = 2.0f * (re10*c + im10*s);
        float out_re11 = 2.0f * (re11*c + im11*s);
        float out_re12 = 2.0f * (re12*c + im12*s);
        float out_re13 = 2.0f * (re13*c + im13*s);
        float out_re14 = 2.0f * (re14*c + im14*s);
        float out_re15 = 2.0f * (re15*c + im15*s);
        float out_re16 = 2.0f * (re16*c + im16*s);

	// TODO: apply transform during write to ddr for 2D transform
        // Combine into one 512-bit DDR word
        ddr buf = 0;
	buf.range( 31,   0) = float_to_bits(out_re1);
	buf.range( 63,  32) = float_to_bits(out_re2);
	buf.range( 95,  64) = float_to_bits(out_re3);
	buf.range(127,  96) = float_to_bits(out_re4);
	buf.range(159, 128) = float_to_bits(out_re5);
	buf.range(191, 160) = float_to_bits(out_re6);
	buf.range(223, 192) = float_to_bits(out_re7);
	buf.range(255, 224) = float_to_bits(out_re8);
	buf.range(287, 256) = float_to_bits(out_re9);
	buf.range(319, 288) = float_to_bits(out_re10);
	buf.range(351, 320) = float_to_bits(out_re11);
	buf.range(383, 352) = float_to_bits(out_re12);
	buf.range(415, 384) = float_to_bits(out_re13);
	buf.range(447, 416) = float_to_bits(out_re14);
	buf.range(479, 448) = float_to_bits(out_re15);
	buf.range(511, 480) = float_to_bits(out_re16);

	mem[n] = buf;
    }
}

}
