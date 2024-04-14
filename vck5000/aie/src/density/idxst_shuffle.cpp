#include "density_kernels.h"

void idxst_shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out) {
    /* Flip order, but preserve first element
    *  Input: a b c d e f g h
    * Output: a h g f e d c b
    */

    FFT_DATA_TYPE data;
    
	data = window_read(in);
	window_writeincr(out, data);
	window_incr(in, POINT_SIZE-1);
    for(int i = 1; i < POINT_SIZE; i++) {
        data = window_read(in);
        window_decr(in, 1);
        window_writeincr(out, data);
	}
}
