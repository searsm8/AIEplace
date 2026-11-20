#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

extern "C"  {

typedef ap_uint<512> ddr;                  // 512-bit DDR word
typedef ap_axiu<64,0,0,0> axis_pkt;           // 64-bit AXI4-Stream word
typedef hls::stream<axis_pkt> aie_stream;

// PL kernel: read from DDR in 512-bit words and send 64-bit beats on AXIS
// 'size' is the number of 64-bit output words to send.
void density_mm2s(const ddr* mem, const unsigned int size,
		aie_stream& s1, aie_stream& s2,
		aie_stream& s3, aie_stream& s4,
		aie_stream& s5, aie_stream& s6,
		aie_stream& s7, aie_stream& s8,
                aie_stream& s9,  aie_stream& s10,
                aie_stream& s11, aie_stream& s12,
                aie_stream& s13, aie_stream& s14,
                aie_stream& s15, aie_stream& s0
		) {
#pragma HLS INTERFACE m_axi     port=mem offset=slave bundle=gmem depth=1024 \
                                max_read_burst_length=16 num_read_outstanding=16
#pragma HLS INTERFACE s_axilite port=mem   bundle=control
#pragma HLS INTERFACE s_axilite port=size  bundle=control
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
#pragma HLS INTERFACE axis      port=s0

    const unsigned int WORDS_PER_BEAT = 16;    // 512 / 32

    DDR_READ_LOOP:
    for (unsigned int n = 0; n < size; ++n) {
    #pragma HLS PIPELINE II=1
	
        // Compute DDR address:
        //   mem[2n]                  for 0 <= n < size/2
        //   mem[2*size - 1 - 2n]     for size/2 <= n < size
        unsigned int addr;
        if (n < (size >> 1)) {
            addr = 2 * n;
        } else {
            addr = 2 * size - 1 - 2 * n;
        }

        ddr wide_word = mem[addr];

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

}
