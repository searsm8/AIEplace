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
void compute_a(input_window_float * in, output_window_float * out_plus, output_window_float * out_minus) {
	aie::vector<float, 8> max_vals, min_vals;
	max_vals = window_readincr_v<8>(in); // first 8 vals are always the max for that net(pre-sorted)

	aie::vector<float, 8> factor = aie::broadcast<float, 8>( 0.0000152587890625 ); // 1 / 2^16
	aie::vector<float, 8> ones   = aie::broadcast<float, 8>( 1.0 );
	aie::vector<float, 8> data;

	// the first output of a+ will always be e^0 = 1
	window_writeincr(out_plus, ones);

	// compute a+ = e ^ (x - x_max)/gamma
	for(int n = 1; n < N; n++) {
		data = window_readincr_v<8>(in);
		// perform fast exp algorithm on all 8 lanes
		data = aie::sub(data, max_vals);
		data = aie::mul((float)inv_gamma, data); // compute (x - x_max) / gamma
		aie::accum<accfloat, 8> acc;
		acc.from_vector(ones, 0);
		acc = aie::mac(acc, data, factor);
		data = acc.to_vector<float>(0); // data now contains: 1 + x/2^16
		for(int i = 0; i < 16; i++)
			data = aie::mul(data, data);
		window_writeincr(out_plus, data);
	}

	window_decr_v8(in, 1);
	min_vals = window_read_v<8>(in); // last 8 vals are always the min for that net(pre-sorted)
	// reset the window before computing a- to ensure correct order
	window_decr_v8(in, N-1);

	// compute a- = e ^ (x_min - x)/gamma
	for(int n = 0; n < N-1; n++) {
		data = window_readincr_v<8>(in);
		// perform fast exp algorithm on all 8 lanes
		data = aie::sub(min_vals, data);
		data = aie::mul((float)inv_gamma, data); // compute (x_min - x) / gamma
		aie::accum<accfloat, 8> acc;
		acc.from_vector(ones, 0);
		acc = aie::mac(acc, data, factor);
		data = acc.to_vector<float>(0); // data now contains: 1 + x/2^16
		for(int i = 0; i < 16; i++)
			data = aie::mul(data, data);
		window_writeincr(out_minus, data);
	}
	// the last output of a- will always be e^0 = 1
	window_writeincr(out_minus, ones);
}
