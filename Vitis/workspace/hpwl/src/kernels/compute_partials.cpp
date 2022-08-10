// compute_partials.cpp
// kernel for computing HPWL partials (see ePlace/DREAMplace)
// dWL_e / dx_i= [a+ * ((1 + x/gamma)*b+ - c+/gamma)/(b+^2)] - [a- * ((1 - x/gamma)*b- + c-/gamma)/(b-^2)]
#include <adf.h>
#include "include.h"
#include "kernels.h"

#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

template <int N>
void compute_partials(
		input_window_float * a_plus_in,
		input_window_float * a_minus_in,
		input_window_float * b_plus_in,
		input_window_float * b_minus_in,
		input_window_float * c_plus_in,
		input_window_float * c_minus_in,
		input_window_float * x_in,
		output_window_float * out) {
	aie::vector<float, 8> c_plus_vec, b_plus_vec, a_plus_vec,
							c_minus_vec, b_minus_vec, a_minus_vec, x_vec, HPWL;
	aie::vector<float, 8> ones   = aie::broadcast<float, 8>( 1.0 );
	aie::vector<float, 8> c_over_gamma, b_squared_inv;
	aie::accum<accfloat, 8> plus_term, minus_term;

	c_plus_vec  = window_readincr_v<8>(c_plus_in);
	b_plus_vec  = window_readincr_v<8>(b_plus_in);
	c_minus_vec = window_readincr_v<8>(c_minus_in);
	b_minus_vec = window_readincr_v<8>(b_minus_in);

	for(int n = 0; n < N; n++) {
		a_plus_vec  = window_readincr_v<8>(a_plus_in);
		a_minus_vec = window_readincr_v<8>(a_minus_in);
		x_vec 	    = window_readincr_v<8>(x_in);
		//x_vec = aie::mul((float)inv_gamma, x_vec);  // x / gamma

		// compute the plus term
		plus_term.from_vector(ones, 0);
		plus_term = aie::mac(plus_term, (float)inv_gamma, x_vec);			// (1 + x/gamma)
		plus_term = aie::mul(plus_term.to_vector<float>(0), b_plus_vec);	// (1 + x/gamma)*b+
		plus_term = aie::msc(plus_term, (float)inv_gamma, c_plus_vec); 		// [((1 + x/gamma)*b+) - (c+ / gamma)]
		b_squared_inv = aie::mul_square(aie::inv(b_plus_vec));				// b+^-2
		plus_term = aie::mul(plus_term.to_vector<float>(0), b_squared_inv);	// [((1 + x/gamma)*b+) - (c+ / gamma)] / b+^-2
		plus_term = aie::mul(plus_term.to_vector<float>(0), a_plus_vec);   	// a+ * [((1 + x/gamma)*b+) - (c+ / gamma)] / b+^2

		// compute the minus term
		minus_term.from_vector(ones, 0);
		minus_term = aie::msc(minus_term, (float)inv_gamma, x_vec);
		minus_term = aie::mul(minus_term.to_vector<float>(0), b_minus_vec);
		minus_term = aie::mac(minus_term, (float)inv_gamma, c_minus_vec);
		b_squared_inv = aie::mul_square(aie::inv(b_minus_vec));
		minus_term = aie::mul(minus_term.to_vector<float>(0), b_squared_inv);

		// subtract and write result
		plus_term = aie::msc(plus_term, minus_term.to_vector<float>(0), a_minus_vec);
		window_writeincr(out, plus_term);
	}
}
