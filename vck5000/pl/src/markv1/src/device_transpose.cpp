#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

#include "system_settings.h" // for POINT_SIZE

extern "C" {
void device_transpose(hls::stream<qdma_axis<128,0,0,0> >& tpose_in, hls::stream<qdma_axis<128,0,0,0> >& tpose_out) {

    #pragma HLS INTERFACE m_axis port=tpose_in
    #pragma HLS INTERFACE m_axis port=tpose_out
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    int values[POINT_SIZE][POINT_SIZE];

    // Read in the matrix result from 1D-DCTs
    for (int i=0; i < POINT_SIZE; i++) {
      for (int j=0; j < POINT_SIZE; j++) {
        qdma_axis<32,0,0,0> val = tpose_in.read();
        values[i][j]=val.data;
      }
    }

    // Write out the matrix by column, which implements a transpose
    for (int i=0; i < POINT_SIZE; i++) {
      for (int j=0; j < POINT_SIZE; j++) {
        qdma_axis<32,0,0,0> x;
        x.data = values[j][i];
        x.keep_all();
        tpose_out.write(x);
      }
    }
}
}
