// compute_a.cpp
// kernel for computing a+ = e ^ (x-x_max) / gamma
#include <adf.h>
#include "include.h"
#include "kernels.h"

//#include "aie_api/aie.hpp"
//#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp> // what is this needed for?

// kernel computes a+ terms for nets
// the first input must always be the max value for that net (either x or y coord)
// the remaining inputs can be in any order
template <int N> // N is the netsize being processed
void compute_a(input_window_float * in, output_window_float * out) {
	aie::vector<float, 8> first_vals;
	first_vals = window_readincr_v<8>(in); // first 8 vals are always the max for that net(pre-sorted)

	aie::vector<float, 8> factor = aie::broadcast<float, 8>( 0.0000152587890625 ); // 1 / 2^16
	aie::vector<float, 8> ones   = aie::broadcast<float, 8>( 1.0 );
	aie::vector<float, 8> data;

	// the first output will always be e^0 = 1
	window_writeincr(out, ones);

	// for all other nodes on the net, compute e ^ (x - x_max)/gamma
	for(int n = 1; n < N; n++) {
		data = window_readincr_v<8>(in);
		// perform fast exp algorithm on all 8 lanes

		if(first_vals[0] > data[0]) // if the max val was given first, compute a+
			data = aie::sub(data, first_vals); // compute (x - x_max) / gamma
		else // min val was given first
			data = aie::sub(first_vals, data); // compute (x_min - x) / gamma

		data = aie::mul((float)inv_gamma, data).to_vector<float>(0);
		// perform fast_exp algorithm
		aie::accum<accfloat, 8> acc;
		acc.from_vector(ones, 0);
		acc = aie::mac(acc, data, factor);
		data = acc.to_vector<float>(0);
		// data now contains: 1 + x/2^16

		for(int i = 0; i < 16; i++)
			data = aie::mul(data, data).to_vector<float>(0);

		window_writeincr(out, data);
	}
}
/*
float fast_exp(float a)
{
	float result = 1 + a / 1024;
	result *= result;
	result *= result;
	result *= result;
	result *= result;
	result *= result;
	result *= result;
	result *= result;
	result *= result;
	result *= result;
	result *= result;
	return result;
}
*/
