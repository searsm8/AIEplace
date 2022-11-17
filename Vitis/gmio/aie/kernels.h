#ifndef FUNCTION_KERNELS_H
#define FUNCTION_KERNELS_H

#define POINT_SIZE 256 // Must be a power of two for FFT
#define FFT_DATA_TYPE cfloat // cfloat, cint32, or cint16
#define TWIDDLE_TYPE cfloat // cfloat, or cint16
// ---EACH LINE OF IO IS ALWAYS 32b---
// if cfloat or cint32 is used, then input format must be:
// real_val_1 (32b)
// imag_val_1 (32b)
// real_val_2 (32b)
// imag_val_2 (32b)
//
// if cint16 is used, then input format is:
// real_val_1 imag_val_1 (32b)
// real_val_2 imag_val_2
// real_val_3 imag_val_3
//

#define PI 3.1415926535

void shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
void dct_postprocess(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

void idct_preprocess(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
void unshuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

void idxst_shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
void idxst_signs(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

// This transpose() function should probably be implemented in a PL kernel
void transpose(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

#endif
