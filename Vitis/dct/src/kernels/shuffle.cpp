
#include <adf.h>
#include "kernels.h"
#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

/* shuffle function for FFT to enable DCT
* for N = 8:
* input = { x0, x1, x2, x3, x4, x5, x6, x7}
* output ={ x0, x2, x4, x6, x7, x5, x3, x1}
*/
void shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out) {
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