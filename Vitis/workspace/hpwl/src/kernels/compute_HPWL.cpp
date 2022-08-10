// compute_HPWL.cpp
// kernel for computing weighted average HPWL = (c+/b+) - (c-/b-)
#include <adf.h>
#include "include.h"
#include "kernels.h"

//#include "aie_api/aie.hpp"
//#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

void compute_HPWL(
		input_window_float * b_plus_in,
		input_window_float * b_minus_in,
		input_window_float * c_plus_in,
		input_window_float * c_minus_in,
		output_window_float * out) {
	aie::vector<float, 8> c_plus_vec, b_plus_vec, c_minus_vec, b_minus_vec, HPWL;
	c_plus_vec  = window_readincr_v<8>(c_plus_in);
	b_plus_vec  = window_readincr_v<8>(b_plus_in);
	c_minus_vec = window_readincr_v<8>(c_minus_in);
	b_minus_vec = window_readincr_v<8>(b_minus_in);

	// perform division by inverse then multiply
	// c/b = c*b_inv
	b_plus_vec = aie::inv(b_plus_vec);
	c_plus_vec = aie::mul(c_plus_vec, b_plus_vec);
	b_minus_vec = aie::inv(b_minus_vec);
	c_minus_vec = aie::mul(c_minus_vec, b_minus_vec);
	HPWL = aie::sub(c_plus_vec, c_minus_vec);
	window_writeincr(out, HPWL);
}
