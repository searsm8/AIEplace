/* A simple kernel
 */
#include <adf.h>
#include "include.h"
#include "kernels.h"
#include <cmath>

//#include "aie_api/aie.hpp"
//#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

#define inv_gamma 0.25


void a_plus(input_window_float * in, output_window_float * out) {
	aie::vector<float, 8> expons;
	expons = window_readincr_v<8>(in);

	// modify exponents by max val and gamma
	//float max_x = aie::reduce_max(x_vec);

	expons = (aie::mul((float)inv_gamma, expons)).to_vector<float>(0);

	// perform fast exp algorithm
	aie::vector<float, 8> factor = aie::broadcast<float, 8>( 0.000976562 );
	aie::vector<float, 8> temp = aie::broadcast<float, 8>( 1.0 );
	aie::accum<accfloat, 8> acc;
	acc.from_vector(temp, 0);
	acc = aie::mac(acc, expons, factor);
	// acc now contains: 1 + x/1024

	for(int i = 0; i < 10; i++) {
		temp = acc.to_vector<float>(0);
		acc = aie::mul(temp, temp);
	}

	window_writeincr(out, acc.to_vector<float>(0));
	//window_writeincr(out, factor);
}

void simple(input_window_float * in, output_window_float * out) {
	float f0, result;
	for(int i = 0; i < 4; i++) {
		window_readincr(in, f0);
		result = exp(f0);
		//result = fast_exp(f0); // ~20x faster!!! ~4% error for exponents < 1
		window_writeincr(out, result);
	}
	}

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
