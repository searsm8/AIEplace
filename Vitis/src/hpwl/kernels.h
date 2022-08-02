
#ifndef FUNCTION_KERNELS_H
#define FUNCTION_KERNELS_H

#define inv_gamma 0.25 // 1/gamma factor for exponents

  template <int N> void compute_a(input_window_float * in, output_window_float * out);
  template <int N> void compute_b(input_window_float * in, output_window_float * out);
  template <int N> void compute_c(input_window_float * x_in, input_window_float * a_in, output_window_float * out);

#endif
