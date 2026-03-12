#include "density_kernels.h"

void idxst_shuffle(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out) {
    /* Flip order, but preserve first element
    *  Input: x0 x1 x2 x3 x4 x5 x6 x7
    * Output: x0 x7 x6 x5 x4 x3 x2 x1
    */
    FFT_DATA_TYPE data [POINT_SIZE];
    
    for(int i = 0; i < POINT_SIZE; i++)
        data[i] = readincr(in);

	writeincr(out, data[0]);
    for(int i = 1; i < POINT_SIZE; i++) {
        writeincr(out, data[POINT_SIZE-i]);
	}
}
