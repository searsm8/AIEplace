
#include <adf.h>
#include "kernels.h"
#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

/*  Transpose kernel outputs a transpose of a matrix
*   Input is expected to be POINT_SIZE*POINT_SIZE long,
*   feeding in the matrix one row at a time.
*   Output is the transposed matrix rows
*   ***For larger matrices and data types, there may not be enough L1 memory***
*/
void transpose(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out) {
    FFT_DATA_TYPE data;
    for(int i = 0; i < POINT_SIZE; i++){
        for(int j = 0; j < POINT_SIZE; j++){
            data = window_read(in);
            window_writeincr(out, data);
            window_incr(in, POINT_SIZE);
        }
        window_incr(in, 1);
    }
}