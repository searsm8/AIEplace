#ifndef FUNCTION_KERNELS_H
#define FUNCTION_KERNELS_H

#define POINT_SIZE 16 // Must be a power of two for FFT
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
//void transpose(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out);


#define inv_gamma 0.25 // 1/gamma factor for exponents (This should probably be a runtime parameter set by the graph)
#define NETSIZE 3

  void compute_a(input_window_float * in, output_window_float * out_plus, output_window_float * out_minus);

  void compute_bc(input_window_float * in_plus, input_window_float * in_minus, \
  					input_window_float * x_in,
					output_window_float * out_b_plus, output_window_float * out_b_minus,
					output_window_float * out_c_plus, output_window_float * out_c_minus);

  void compute_HPWL(input_window_float * b_plus_in,  input_window_float * b_minus_in,
		  	  	  	input_window_float * c_plus_in, input_window_float * c_minus_in, output_window_float * out);

  void compute_partials(  input_window_float * a_plus_in, input_window_float * a_minus_in,
 	  	  	  	  	  	  input_window_float * b_plus_in, input_window_float * b_minus_in,
 	  	  	  	  	  	  input_window_float * c_plus_in, input_window_float * c_minus_in,
						  input_window_float * x_in, output_window_float * out);

#endif
