#ifndef DENSITY_KERNELS_H
#define DENSITY_KERNELS_H

#include "system_settings.h"
using namespace adf;

// ********************
// Density compute kernels
// ********************

// DCT
void dct_shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
void dct_postprocess(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

// IDCT
void idct_preprocess(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
void idct_unshuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

// IDXST
void idxst_shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
void idxst_signs(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

// This transpose() function should probably be implemented in a PL kernel
//void transpose(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

#endif
