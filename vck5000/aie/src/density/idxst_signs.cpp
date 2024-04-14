
#include "density_kernels.h"

void idxst_signs(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out) {
	float signs_init[8]={1,-1,1,-1,1,-1,1,-1};
    aie::vector<float, 8> signs = aie::load_v<8>(signs_init); 
    for(int i = 0; i < POINT_SIZE/8; i++) {
        aie::vector<FFT_DATA_TYPE, 8> data = aie::mul(window_readincr_v<8>(in), signs);
        window_writeincr(out, data);
    }
}