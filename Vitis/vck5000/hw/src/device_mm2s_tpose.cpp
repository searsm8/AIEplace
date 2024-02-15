#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

#include "system_settings.h" // for POINT_SIZE

extern "C" {
void device_mm2s_tpose(ap_int<32> * density_input, hls::stream<qdma_axis<128,0,0,0> > & stream_aie2pl, hls::stream<qdma_axis<128,0,0,0> > & stream_pl2aie) {

    #pragma HLS INTERFACE m_axi port=density_input offset=slave
    #pragma HLS INTERFACE m_axis port=stream_pl2aie
    #pragma HLS INTERFACE s_axilite port=input bundle=control
    //#pragma HLS INTERFACE s_axilite port=size bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    // Write density inputs to the AIE array
    // Preprocess by performing shuffle
    for (int i=0; i < POINT_SIZE; i++) {
      for (int j=0; j < POINT_SIZE; j++) {
        qdma_axis<32,0,0,0> x;
        x.data = density_input[i*POINT_SIZE + j];
        x.keep_all();
        stream_pl2aie.write(x);
      }
    }

    
}
}