#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

extern "C" {
void device_s2mm(ap_int<128> * output, int size, hls::stream<qdma_axis<128,0,0,0> >& stream_out) {

    #pragma HLS INTERFACE m_axi port=output offset=slave
    #pragma HLS INTERFACE m_axis port=stream_out
    #pragma HLS INTERFACE s_axilite port=output bundle=control
    #pragma HLS INTERFACE s_axilite port=size bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    int els=size/4;
    if (els * 4 < size) els++;

    for (int i=0;i<els;i++) {
      qdma_axis<128,0,0,0> iv=stream_out.read();
      output[i]=iv.data;
    }
}
}
