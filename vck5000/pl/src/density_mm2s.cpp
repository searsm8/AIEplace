#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

extern "C" {
void density_mm2s(ap_int<128> * input, int size, hls::stream<qdma_axis<128,0,0,0> >& stream_pl2aie) {

    #pragma HLS INTERFACE m_axi port=input offset=slave
    #pragma HLS INTERFACE m_axis port=stream_pl2aie
    #pragma HLS INTERFACE s_axilite port=input bundle=control
    #pragma HLS INTERFACE s_axilite port=size bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    int els = size/4;
    if (els * 4 < size) els++;

    for (int i=0; i<els; i++) {
      qdma_axis<128,0,0,0> x;
      x.data=input[i];
      x.keep_all();
      stream_pl2aie.write(x);
    }
}
}

//void sent_ctrl_data(int size, float add_value, hls::stream<qdma_axis<32,0,0,0> >& stream_ctrl) {
//  ap_int<32> data=*((ap_int<32>*) &(size));
//  qdma_axis<32,0,0,0> qdma_packet;
//  qdma_packet.data=data;
//  qdma_packet.keep_all();
//  stream_ctrl.write(qdma_packet);
//
//  ap_int<32> d2=*((ap_int<32>*) &(add_value));
//  qdma_axis<32,0,0,0> qdma_p;
//  qdma_p.data=d2;
//  qdma_p.keep_all();
//  stream_ctrl.write(qdma_p);
//}
