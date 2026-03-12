#include "density_kernels.h"

/* unshuffle function for IFFT to enable IDCT
* for N = 8:
* input = { x0, x2, x4, x6, x7, x5, x3, x1}
* output ={ x0, x1, x2, x3, x4, x5, x6, x7}
*/
#define HALF_POINT_SIZE POINT_SIZE/2
void idct_unshuffle(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out) {
    FFT_DATA_TYPE data_even [HALF_POINT_SIZE];
    FFT_DATA_TYPE data_odd [HALF_POINT_SIZE];

    for(int i = 0; i < HALF_POINT_SIZE; i++)
        data_even[i] = readincr(in);

    for(int i = 0; i < HALF_POINT_SIZE; i++)
        data_odd[i] = readincr(in);

    // latter half: choose odd inputs, decreasing
    for(int i = 0; i < HALF_POINT_SIZE; i++){
        writeincr(out, data_even[i]);
        writeincr(out, data_odd[HALF_POINT_SIZE-i-1]);
    }
}