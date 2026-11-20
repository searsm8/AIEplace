#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>
#include <cstdint>

#include "twiddle_table.h"

#define DDR_WORDS_PER_ROW 64 // 1024 floats -> 32768 bits: 32768/512 = 64 ddr words per row

//TODO: make ENUM
#define MODE_DCT 0

extern "C"  {

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


// PL kernel: read from DDR in 512-bit words and send 64-bit beats on AXIS
// 'size' is the number of 64-bit output words to send.
void density_mm2s(const ddr* mem, const unsigned int size, const unsigned int offset,
		aie_stream& s1, aie_stream& s2,
		aie_stream& s3, aie_stream& s4,
		aie_stream& s5, aie_stream& s6,
		aie_stream& s7, aie_stream& s8,
                aie_stream& s9,  aie_stream& s10,
                aie_stream& s11, aie_stream& s12,
                aie_stream& s13, aie_stream& s14,
                aie_stream& s15, aie_stream& s0
		) {

    const unsigned int WORDS_PER_BEAT = 16;    // 512 / 32

    DDR_READ_LOOP:
    for (unsigned int n = 0; n < size; ++n) {
    #pragma HLS PIPELINE II=1
	
        // Compute DDR address:
        //   mem[2n]                  for 0 <= n < size/2
        //   mem[2*size - 1 - 2n]     for size/2 <= n < size
        unsigned int logical_addr;

        if (n < (size >> 1)) {
            logical_addr = 2 * n;
        } else {
            logical_addr = 2 * size - 1 - 2 * n;
        }

        const unsigned int base_addr = logical_addr * DDR_WORDS_PER_ROW;
        ddr wide_word = mem[base_addr + offset];

        axis_pkt v[WORDS_PER_BEAT];
    #pragma HLS ARRAY_PARTITION variable=v complete

        // send the 16 32-bit real parts from the 512-bit mem read
	// to the 16 different channels in a 64-bit packet, where
	// the LSB are the imaginary part set to 0.
        for (unsigned int k = 0; k < WORDS_PER_BEAT; ++k) {
        #pragma HLS UNROLL
	    v[k].data.range(31, 0)  = wide_word.range(32*k + 31, 32*k);      // Real
	    v[k].data.range(63, 32) = 0;                                     // Imaginary = 0.0f
            v[k].keep = -1;    // all 8 bytes valid
            v[k].strb = -1;    // all 8 bytes active
            v[k].last = (n == size - 1);
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

// PL kernel: receive 64-bit words from AXIS and write to DDR as 512-bit beats
// 'size' is the number of 64-bit words expected from the stream. Currently ignored and fixed to 1024. Remove in the future
void density_s2mm(ddr* mem, const unsigned int size, const unsigned int offset,
		aie_stream& s1,aie_stream& s2,
		aie_stream& s3,aie_stream& s4,
		aie_stream& s5,aie_stream& s6,
		aie_stream& s7,aie_stream& s8,
                aie_stream& s9,  aie_stream& s10,
                aie_stream& s11, aie_stream& s12,
                aie_stream& s13, aie_stream& s14,
                aie_stream& s15, aie_stream& s0
		) {

	const unsigned int VALUES_PER_WORD = 16;
	const unsigned int words_per_row = size / VALUES_PER_WORD;

	// Each bank contains one 512-bit word for each destination row.
	ddr ping[16];
	ddr pong[16];
#pragma HLS ARRAY_PARTITION variable=ping complete
#pragma HLS ARRAY_PARTITION variable=pong complete

DDR_WRITE_LOOP:
    for (unsigned int n = 0; n < size; ++n) {
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
        float re1, im1, re2, im2, re3, im3, re4, im4;
        float re5, im5, re6, im6, re7, im7, re8, im8;
        float re9, im9, re10, im10, re11, im11, re12, im12;
        float re13, im13, re14, im14, re15, im15, re0, im0;

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
        // 2 * (re + j im) * (c - j s) = 2 * ((re*c + im*s) + j(im*c - re*s))
	// We only need the real part for DCT, so only calculate 2 * (re*c + im*s)
        float out_re0  = 2.0f * (re0*c + im0*s);
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

	// Write data back in transposed form
	ap_uint<32> output_bits[16];
#pragma HLS ARRAY_PARTITION variable=output_bits complete

	output_bits[0]  = float_to_bits(out_re0);
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
			const unsigned int row = offset * 16 + write_lane;
			const unsigned int address = row * words_per_row + (block - 1);

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
		const unsigned int row = offset * 16 + write_lane;
		const unsigned int address = row * words_per_row + (block - 1);

		mem[address] = ping[write_lane];
	}
    }

    // The final bank has been filled but has not yet been written.
    const unsigned int final_block = (size >> 4) - 1;

DRAIN_FINAL_BANK:
    for (unsigned int lane = 0; lane < 16; ++lane) {
#pragma HLS PIPELINE II=1
	    const unsigned int row = offset * 16 + lane;
	    const unsigned int address =
		    row * words_per_row + final_block;

	    if ((final_block & 1) == 0) {
		    mem[address] = ping[lane];
	    } else {
		    mem[address] = pong[lane];
	    }
    }
}

void run_single_pass(
    ddr* src,
    ddr* dst,
    unsigned int size,
    unsigned int offset,

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
#pragma HLS INLINE off
#pragma HLS DATAFLOW
	density_mm2s(src, size, offset, o1,o2,o3,o4,o5,o6,o7,o8,o9,o10,o11,o12,o13,o14,o15,o0);
	density_s2mm(dst, size, offset, i1,i2,i3,i4,i5,i6,i7,i8,i9,i10,i11,i12,i13,i14,i15,i0);
}


void run_2d_pass(ddr* buf0, ddr* buf1, const unsigned int size, int mode,
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
	for (unsigned int offset = 0; offset < DDR_WORDS_PER_ROW; offset++ ) {
		run_single_pass(buf0, buf1, size, offset, o1,o2,o3,o4,o5,o6,o7,o8,o9,o10,o11,o12,o13,o14,o15,o0, i1,i2,i3,i4,i5,i6,i7,i8,i9,i10,i11,i12,i13,i14,i15,i0);
	}
	for (unsigned int offset = 0; offset < DDR_WORDS_PER_ROW; offset++ ) {
		run_single_pass(buf1, buf0, size, offset, o1,o2,o3,o4,o5,o6,o7,o8,o9,o10,o11,o12,o13,o14,o15,o0, i1,i2,i3,i4,i5,i6,i7,i8,i9,i10,i11,i12,i13,i14,i15,i0);
	}
}

void dct2d_accelerator(
		ddr* buf0,
		ddr* buf1,
		const unsigned int size,

		// 16 streams from PL to AIE
		aie_stream& o1, aie_stream& o2,
		aie_stream& o3, aie_stream& o4,
		aie_stream& o5, aie_stream& o6,
		aie_stream& o7, aie_stream& o8,
		aie_stream& o9,  aie_stream& o10,
		aie_stream& o11, aie_stream& o12,
		aie_stream& o13, aie_stream& o14,
		aie_stream& o15, aie_stream& o0,
		// 16 streams from AIE to PL
		aie_stream& i1, aie_stream& i2,
		aie_stream& i3, aie_stream& i4,
		aie_stream& i5, aie_stream& i6,
		aie_stream& i7, aie_stream& i8,
		aie_stream& i9,  aie_stream& i10,
		aie_stream& i11, aie_stream& i12,
		aie_stream& i13, aie_stream& i14,
		aie_stream& i15, aie_stream& i0
		) {
#pragma HLS INTERFACE m_axi port=buf0 bundle=gmem0 depth=65536
#pragma HLS INTERFACE m_axi port=buf1 bundle=gmem1 depth=65536

#pragma HLS INTERFACE s_axilite port=buf0       bundle=control
#pragma HLS INTERFACE s_axilite port=buf1       bundle=control
#pragma HLS INTERFACE s_axilite port=size       bundle=control
#pragma HLS INTERFACE s_axilite port=return     bundle=control

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
			run_2d_pass(buf0, buf1, size, MODE_DCT, o0,o1,o2,o3,o4,o5,o6,o7,o8,o9,o10,o11,o12,o13,o14,o15, i0,i1,i2,i3,i4,i5,i6,i7,i8,i9,i10,i11,i12,i13,i14,i15);

			//remove_dc(buf0);

			//run_2d_pass(buf0, buf1, MODE_IDCT);
			//run_2d_pass(buf1, buf0, MODE_IDCT);
		}

} // extern "C"
