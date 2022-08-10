
#ifndef FUNCTION_KERNELS_H
#define FUNCTION_KERNELS_H

#define inv_gamma 0.25 // 1/gamma factor for exponents (This should probably be a runtime parameter set by the graph)

  template <int N> void compute_a(input_window_float * in, output_window_float * out_plus, output_window_float * out_minus);

  template <int N> void compute_bc(input_window_float * in_plus, input_window_float * in_minus, input_window_float * x_in,
		  	  	  	  	  	  	   output_window_float * out_b_plus, output_window_float * out_b_minus,
								   output_window_float * out_c_plus, output_window_float * out_c_minus);

  void compute_HPWL(input_window_float * b_plus_in,  input_window_float * b_minus_in,
		  	  	  	input_window_float * c_plus_in, input_window_float * c_minus_in, output_window_float * out);

  template <int N> void compute_partials( input_window_float * a_plus_in, input_window_float * a_minus_in,
		  	  	  	  	  	  	  	  	  input_window_float * c_plus_in, input_window_float * c_minus_in,
		  	  	  	  	  	  	  	  	  input_window_float * b_plus_in, input_window_float * b_minus_in,
										  input_window_float * x_in,       output_window_float * out);

#endif
