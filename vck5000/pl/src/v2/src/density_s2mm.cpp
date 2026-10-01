#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

extern "C"  {

typedef ap_uint<512> ddr;                  // 512-bit DDR word
typedef ap_axiu<64,0,0,0> axis_pkt;           // 64-bit AXI4-Stream word
typedef hls::stream<axis_pkt> aie_stream;

// Pack two floats into 64 bits: [63:32] = real, [31:0] = imag
static ap_uint<64> pack_complex(float re, float im) {
#pragma HLS INLINE
    ap_uint<32> re_bits = *reinterpret_cast<ap_uint<32>*>(&re);
    ap_uint<32> im_bits = *reinterpret_cast<ap_uint<32>*>(&im);

    ap_uint<64> out = 0;
    out.range(63, 32) = re_bits;
    out.range(31, 0)  = im_bits;
    return out;
}

// Unpack 64 bits into two floats: [31:0] -> imag, [63:32] -> real
static void unpack_complex(ap_uint<64> in, float &re, float &im) {
#pragma HLS INLINE
    ap_uint<32> re_bits = in.range(63, 32);
    ap_uint<32> im_bits = in.range(31, 0);

    re = *reinterpret_cast<float*>(&re_bits);
    im = *reinterpret_cast<float*>(&im_bits);
}

// PL kernel: receive 64-bit words from AXIS and write to DDR as 512-bit beats
// 'size' is the number of 64-bit words expected from the stream. Currently ignored and fixed to 1024. Remove in the future
void density_s2mm(ddr* mem, const unsigned int size,
		aie_stream& s1,aie_stream& s2,
		aie_stream& s3,aie_stream& s4,
		aie_stream& s5,aie_stream& s6,
		aie_stream& s7,aie_stream& s8
		) {
#pragma HLS INTERFACE m_axi     port=mem offset=slave bundle=gmem depth=64 \
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

    const unsigned int N = 1024;

DDR_WRITE_LOOP:
    for (unsigned int n = 0; n < N; ++n) {
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

        // Combine into one 512-bit DDR word
        ddr buf = 0;
        buf.range( 63,   0) = p1;
        buf.range(127,  64) = p2;
        buf.range(191, 128) = p3;
        buf.range(255, 192) = p4;
        buf.range(319, 256) = p5;
        buf.range(383, 320) = p6;
        buf.range(447, 384) = p7;
        buf.range(511, 448) = p8;

        mem[n] = buf;
    }
}

}
