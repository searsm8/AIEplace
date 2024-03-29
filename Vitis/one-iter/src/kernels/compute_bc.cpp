// compute_bc.cpp
// kernel for computing b+, b-, c+ and c-
#include <adf.h>
#include "kernels.h"

#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

// Inputs are a+, a- and x coord values. each lane is for a net, so 8 nets are processed simultaneously
// Each lane is for a net, so 8 nets are processed simultaneously
void compute_bc(input_window_float * in_a_plus, input_window_float * in_a_minus,
				input_window_float * x_in,
				output_window_float * out_b_plus, output_window_float * out_b_minus,
				output_window_float * out_c_plus, output_window_float * out_c_minus) {
	aie::accum<accfloat, 8> acc;
	// Compute b+
	acc.from_vector(aie::zeros<float, 8>(), 0);
	for(int n = 0; n < NETSIZE; n++)
		acc = aie::add(acc.to_vector<float>(0), window_readincr_v<8>(in_a_plus));
	window_writeincr(out_b_plus, acc.to_vector<float>(0));

	// Compute b-
	acc.from_vector(aie::zeros<float, 8>(), 0);
	for(int n = 0; n < NETSIZE; n++)
		acc = aie::add(acc.to_vector<float>(0), window_readincr_v<8>(in_a_minus));
	window_writeincr(out_b_minus, acc.to_vector<float>(0));

	// a+, a- windows loop around, so no need to adjust window positions
	// Compute c+
	acc.from_vector(aie::zeros<float, 8>(), 0);
	for(int n = 0; n < NETSIZE; n++)
		acc = aie::mac(acc, window_readincr_v<8>(x_in), window_readincr_v<8>(in_a_plus));
	window_writeincr(out_c_plus, acc.to_vector<float>(0));

	// Compute c-
	acc.from_vector(aie::zeros<float, 8>(), 0);
	for(int n = 0; n < NETSIZE; n++)
		acc = aie::mac(acc, window_readincr_v<8>(x_in), window_readincr_v<8>(in_a_minus));
	window_writeincr(out_c_minus, acc.to_vector<float>(0));
}
