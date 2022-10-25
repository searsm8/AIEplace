
#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>


extern "C" {

// PL kernel which reads a matrix from a stream of data
// and performs a matrix transpose, then writes to out stream 

//PERHAPS "mem" COULD BE TYPE cfloat
// It might not matter so long as number of bits is correct (32 or 64)
void transpose(ap_int<32>* mem, hls::stream<qdma_axis<32, 0, 0, 0>  >& in, hls::stream<qdma_axis<32, 0, 0, 0>  >& out, int num_rows, int num_cols) {
#pragma HLS INTERFACE m_axi port=mem offset=slave bundle=gmem

#pragma HLS interface axis port=in
#pragma HLS interface axis port=out

#pragma HLS INTERFACE s_axilite port=mem bundle=control
#pragma HLS INTERFACE s_axilite port=num_rows bundle=control
#pragma HLS INTERFACE s_axilite port=num_cols bundle=control
#pragma HLS interface s_axilite port=return bundle=control

	qdma_axis<32, 0, 0, 0> x;
	// Read in stream and perform transpose while writing to memory
	for(int col = 0; col < num_cols; col++) {
	#pragma HLS PIPELINE II=1
		for(int row = 0; row < num_rows; row++) {
			x = in.read();
			mem[2*(col + row*num_cols)] = x.data; // write the real part transposed
			x = in.read();
			mem[2*(col + row*num_cols) + 1] = x.data; // The imaginary part tags along with the real part
		}
	}

	// Write matrix to output stream
	for(int i = 0; i < num_rows*num_cols*2; i++) {
	#pragma HLS PIPELINE II=1
		x.data = mem[i];
		x.keep_all();
		out.write(x);
	}
}
}
