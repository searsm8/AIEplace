#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

extern "C" {
void device_mm2aie2mm(float * input, float * output, int size, hls::stream<qdma_axis<128,0,0,0> >& stream_pl2aie,  hls::stream<qdma_axis<128,0,0,0> >& stream_aie2pl) {

    #pragma HLS INTERFACE m_axi port=input offset=slave
    #pragma HLS INTERFACE m_axi port=output offset=slave
    #pragma HLS INTERFACE m_axis port=stream_pl2aie
    #pragma HLS INTERFACE m_axis port=stream_aie2pl
    #pragma HLS INTERFACE s_axilite port=input bundle=control
    #pragma HLS INTERFACE s_axilite port=output bundle=control
    #pragma HLS INTERFACE s_axilite port=size bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    int els = size/4;
    if (els * 4 < size) els++;

    // Receive input from host and write to aie stream
    for (int i=0; i<els+1; i++) {
      qdma_axis<128,0,0,0> x;
      x.data=input[i];
      x.keep_all();
      stream_pl2aie.write(x);
    }

    // Receive results from aie stream and write to output buffer
    for (int i=0; i<els; i++) {
      qdma_axis<128,0,0,0> iv=stream_aie2pl.read();
      output[i]=iv.data;
    }
}
}