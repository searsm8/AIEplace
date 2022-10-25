/**********
© Copyright 2020 Xilinx, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
**********/


#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>


extern "C" {

// Stream to Memory Mapped
// PL kernel which reads a matrix from a stream of data
// and writes it to memory
void s2mm(ap_int<32>* mem, hls::stream<qdma_axis<32, 0, 0, 0>  >& s, int num_rows, int num_cols) {
#pragma HLS INTERFACE m_axi port=mem offset=slave bundle=gmem

#pragma HLS interface axis port=s

#pragma HLS INTERFACE s_axilite port=mem bundle=control
#pragma HLS INTERFACE s_axilite port=num_rows bundle=control
#pragma HLS INTERFACE s_axilite port=num_cols bundle=control
#pragma HLS interface s_axilite port=return bundle=control

	for(int row = 0; row < num_rows; row++) {
	#pragma HLS PIPELINE II=1
		for(int col = 0; col < num_cols; col++) {
			qdma_axis<32, 0, 0, 0> x = s.read();
			mem[col + row*num_cols] = x.data;
		}
	}

}
}
