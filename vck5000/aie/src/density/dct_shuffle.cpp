
#include "density_kernels.h"

/* shuffle function used to precondition data
* for N = 8:
* input = { x0, x1, x2, x3, x4, x5, x6, x7}
* output ={ x0, x2, x4, x6, x7, x5, x3, x1}
*
* DCT = Re[FFT_shuf * e^(i*alpha)]
* FFT_shuf = FFT performed on shuffled inputs
*/
//vectorized implementation
void shuffle_vectorized(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out) {
    FFT_DATA_TYPE data;

}

// Stream Implementation
#ifdef USE_STREAM_IO
void dct_shuffle(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out) {

    FFT_DATA_TYPE data_even;
    FFT_DATA_TYPE data_odd [POINT_SIZE/2];

    // first half: choose even inputs, increasing
    for(int i = 0; i < POINT_SIZE/2; i++){
        data_even = readincr(in);
        writeincr(out, data_even);
        data_odd[i] = readincr(in);
    }

    // latter half: choose odd inputs, decreasing
    for(int i = POINT_SIZE/2 - 1; i >= 0; i--){
        writeincr(out, data_odd[i]);
    }
}

#else
//Window implementation
void dct_shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out) {
    FFT_DATA_TYPE data;
    
    data = window_read(in);
    window_writeincr(out, data);
    // first half: choose every other input, increasing
    for(int i = 1; i < POINT_SIZE/2; i++){
        window_incr(in, 2);
        data = window_read(in);
        window_writeincr(out, data);
    }

    window_incr(in, 1);
    data = window_read(in);
    window_writeincr(out, data);

    // latter half: choose every other input, decreasing
    for(int i = 1; i < POINT_SIZE/2; i++){
        window_decr(in, 2);
        data = window_read(in);
        window_writeincr(out, data);
    }
}
#endif