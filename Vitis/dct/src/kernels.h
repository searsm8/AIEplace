
#ifndef FUNCTION_KERNELS_H
#define FUNCTION_KERNELS_H
//#include "system_settings.h"

#define POINT_SIZE 16  // Must be a power of two for FFT
#define FFT_DATA_TYPE cfloat // cfloat, cint32, or cint16
#define DCT_DATA_TYPE float // float, int32, or int16
#define TWIDDLE_TYPE cfloat // cfloat, or cint16
// ---EACH LINE OF IO IS ALWAYS 32b---
// if cfloat or cint32 is used, then input format must be:
// real value 1 (32b)
// imag value 1
// real value 2
// imag value 2
//
// if cint16 is used, then input format is:
// (real value 1) (imag value 1) (32b)
// (real value 2) (imag value 2)
// (real value 3) (imag value 3)
//

#define PI 3.1415926535

void fft_to_dct(input_window<FFT_DATA_TYPE> * in, output_window<DCT_DATA_TYPE> * out);
void shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

#endif
