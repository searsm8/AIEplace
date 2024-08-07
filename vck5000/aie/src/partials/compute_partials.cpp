// compute_partials.cpp
// kernel for computing HPWL partials (see ePlace/DREAMplace)
// dWL_e / dx_i= [a+ * ((1 + x/gamma)*b+ - c+/gamma)/(b+^2)]  \
// - [a- * ((1 - x/gamma)*b- + c-/gamma)/(b-^2)]
#include "partials_kernels.h"

#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

void compute_partials( input_stream<float> * __restrict xa_in, input_stream<float> * __restrict bc_in, output_stream<float> * __restrict partials_out)
{
	// Read control data
	aie::vector<float, 8> ctrl;
	ctrl.insert(0,readincr_v<4>(bc_in));
	ctrl.insert(1,readincr_v<4>(bc_in));
	float net_size  = ctrl.get(0);
	float packet_groups = ctrl.get(1);
	// ignore ctrl(2) thru ctrl(7)

	aie::vector<float, 8> a_plus, b_plus, c_plus, a_minus, b_minus, c_minus, x_vals;
	aie::vector<float, 8> ones   = aie::broadcast<float, 8>( 1.0 );
	aie::vector<float, 8> c_over_gamma, b_squared_inv; // intermediate results
	aie::accum<accfloat, 8> plus_term, minus_term;

	for(int i = 0; i < packet_groups; i++) {

		// read in bc stream for a vector of 8 nets
		b_plus = readincr_v<8>(bc_in);
		c_plus = readincr_v<8>(bc_in);
		b_minus = readincr_v<8>(bc_in);
		c_minus = readincr_v<8>(bc_in);

		// compute partials for each x val on these nets
		for(int n = 0; n < net_size; n++) {
			// read in xa stream values
			x_vals = readincr_v<8>(xa_in); // 4 sets of 32b flot
			a_plus = readincr_v<8>(xa_in);
			a_minus = readincr_v<8>(xa_in);

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
			writeincr(partials_out, plus_term.to_vector<float>(0));
	  	}
	}
}
