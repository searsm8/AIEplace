#ifndef DENSITY_KERNELS_H
#define DENSITY_KERNELS_H

#include "system_settings.h"
using namespace adf;

// ********************
// Density compute kernels
// ********************

#define USE_STREAM_IO

#ifdef USE_STREAM_IO
    void dct_shuffle(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out);
    void dct_postprocess(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out);
    void idct_preprocess(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out);
    void idct_unshuffle(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out);
    void idxst_shuffle(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out);
    void idxst_signs(input_stream<FFT_DATA_TYPE> * in, output_stream<FFT_DATA_TYPE> * out);
#else
    void dct_shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
    void dct_postprocess(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
    void idct_preprocess(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
    void idct_unshuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
    void idxst_shuffle(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
    void idxst_signs(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);
#endif

// This transpose() function should probably be implemented in a PL kernel since AIE cores
// have insufficient memory to hold 1024x1024 float array
// FUTURE WORK: it would be very beneficial to be able to do this transpose in AIEs! 
//void transpose(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);

#endif
