
#include "density_kernels.h"


/* @brief Negate every other element
 *  This is equivalent to multiplying by (-1)^k
 *
 */
void idxst_signs(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out) {
	float signs_init[8]={1,-1,1,-1,1,-1,1,-1};
    aie::vector<float, 8> signs = aie::load_v<8>(signs_init); 
    for(int i = 0; i < POINT_SIZE/8; i++) {
        aie::vector<FFT_DATA_TYPE, 8> data = aie::mul(readincr_v<8>(in), signs);
        writeincr(out, data);
    }
}