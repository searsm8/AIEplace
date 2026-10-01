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
		aie_stream& s7, aie_stream& s8
		) {
#pragma HLS INTERFACE m_axi     port=mem offset=slave bundle=gmem depth=64 \
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

    const unsigned int WORDS_PER_BEAT = 8;    // 512 / 64

    DDR_READ_LOOP:
    for (unsigned int n = 0; n < size; ++n) {
    #pragma HLS PIPELINE II=1

        ddr wide_word = mem[n];

        axis_pkt v[WORDS_PER_BEAT];
    #pragma HLS ARRAY_PARTITION variable=v complete

        // Prepare up to 8×64-bit words from this 512-bit beat
        for (unsigned int k = 0; k < WORDS_PER_BEAT; ++k) {
        #pragma HLS UNROLL
            v[k].data = wide_word.range(64*(k+1)-1, 64*k);
            v[k].keep = -1;    // all 8 bytes valid
            v[k].strb = -1;    // all 8 bytes active
            v[k].last = (n == size - 1);
        }

        // Send each slice to its corresponding stream
        s1.write(v[0]);
        s2.write(v[1]);
        s3.write(v[2]);
        s4.write(v[3]);
        s5.write(v[4]);
        s6.write(v[5]);
        s7.write(v[6]);
        s8.write(v[7]);
    }
}

}
