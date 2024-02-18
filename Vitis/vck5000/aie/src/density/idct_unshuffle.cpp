#include "density_kernels.h"

/* unshuffle function for IFFT to enable IDCT
* for N = 8:
* input = { x0, x2, x4, x6, x7, x5, x3, x1}
* output ={ x0, x1, x2, x3, x4, x5, x6, x7}
*/
void idct_unshuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out) {
    FFT_DATA_TYPE data;
	//aie::vector<float, 16> real_data;
    
    for(int i = POINT_SIZE; i > 0;){
        data = window_read(in);
        data.imag = 0; // Take only the real part
        window_writeincr(out, data);
        window_incr(in, --i);
        data = window_read(in);
        data.imag = 0;
        window_writeincr(out, data);
        window_decr(in, --i);

        // Take only the real part
		//real_data = data.cast_to<float>();
		////result = aie::filter_even(real_data);
		//for(int i = 0; i < 8; i++)
		//	real_data[2*i+1] = 0; // Set imaginary part to zero
		//data = real_data.cast_to<cfloat>();
    }
}