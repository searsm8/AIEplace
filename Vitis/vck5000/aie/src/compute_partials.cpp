// compute_partials.cpp
// kernel for computing HPWL partials (see ePlace/DREAMplace)
// dWL_e / dx_i= [a+ * ((1 + x/gamma)*b+ - c+/gamma)/(b+^2)]  \
// - [a- * ((1 - x/gamma)*b- + c-/gamma)/(b-^2)]
#include <adf.h>
#include "kernels.h"

#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

void compute_partials( input_stream<float> * __restrict xa_in, input_stream<float> * __restrict bc_in, output_stream<float> * __restrict partials_out)
{
	// Read control data
	aie::vector<float, 4> ctrl = readincr_v4(xa_in);
	float net_size  = ctrl.get(0);
	float net_count = ctrl.get(1); // will always be multiple of 8?
	//float net_size  = 3;
	//float net_count = 8;

	aie::vector<float, 8> a_plus, b_plus, c_plus, a_minus, b_minus, c_minus, x_vals;
	aie::vector<float, 8> ones   = aie::broadcast<float, 8>( 1.0 );
	aie::vector<float, 8> c_over_gamma, b_squared_inv; // intermediate results
	aie::accum<accfloat, 8> plus_term, minus_term;

	for(int i = 0; i < net_count/8; i++) { // will always be multiple of 8?
		// read in bc stream for a vector of 8 nets
		b_plus.insert(0, readincr_v<4>(bc_in));
		b_plus.insert(1, readincr_v<4>(bc_in));
		c_plus.insert(0, readincr_v<4>(bc_in));
		c_plus.insert(1, readincr_v<4>(bc_in));
		b_minus.insert(0, readincr_v<4>(bc_in));
		b_minus.insert(1, readincr_v<4>(bc_in));
		c_minus.insert(0, readincr_v<4>(bc_in));
		c_minus.insert(1, readincr_v<4>(bc_in));

		// compute partials for each x val on these nets
		for(int n = 0; n < net_size; n++) {
			// read in xa stream values
			x_vals.insert(0, readincr_v<4>(xa_in)); // 4 sets of 32b float
			x_vals.insert(1, readincr_v<4>(xa_in));
			a_plus.insert(0, readincr_v<4>(xa_in));
			a_plus.insert(1, readincr_v<4>(xa_in));
			a_minus.insert(0, readincr_v<4>(xa_in));
			a_minus.insert(1, readincr_v<4>(xa_in));

			// compute the plus term
			plus_term.from_vector(ones, 0);
			plus_term = aie::mac(plus_term, (float)inv_gamma, x_vals);			// (1 + x/gamma)
			plus_term = aie::mul(plus_term.to_vector<float>(0), b_plus);	// (1 + x/gamma)*b+
			plus_term = aie::msc(plus_term, (float)inv_gamma, c_plus); 		// [((1 + x/gamma)*b+) - (c+ / gamma)]
			b_squared_inv = aie::mul_square(aie::inv(b_plus));				// b+^-2
			plus_term = aie::mul(plus_term.to_vector<float>(0), b_squared_inv);	// [((1 + x/gamma)*b+) - (c+ / gamma)] / b+^-2
			plus_term = aie::mul(plus_term.to_vector<float>(0), a_plus);   	// a+ * [((1 + x/gamma)*b+) - (c+ / gamma)] / b+^2

			// compute the minus term
			minus_term.from_vector(ones, 0);
			minus_term = aie::msc(minus_term, (float)inv_gamma, x_vals);
			minus_term = aie::mul(minus_term.to_vector<float>(0), b_minus);
			minus_term = aie::mac(minus_term, (float)inv_gamma, c_minus);
			b_squared_inv = aie::mul_square(aie::inv(b_minus));
			minus_term = aie::mul(minus_term.to_vector<float>(0), b_squared_inv);

			// subtract and write result
			plus_term = aie::msc(plus_term, minus_term.to_vector<float>(0), a_minus);
			writeincr(partials_out, plus_term.to_vector<float>(0).extract<4>(0));
			writeincr(partials_out, plus_term.to_vector<float>(0).extract<4>(1));
	  	}
	}
}
