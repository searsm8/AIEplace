// compute_partials.cpp
// kernel for computing HPWL partials (see ePlace/DREAMplace)
// dWL_e / dx_i= [a+ * ((1 + x/gamma)*b+ - c+/gamma)/(b+^2)]  \
// - [a- * ((1 - x/gamma)*b- + c-/gamma)/(b-^2)]
#include "partials/kernels.h"

#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

#define MAX_NET_SIZE 8 // Max number of nodes per net that this kernel can handle. 
						// This allows buffers to be allocated statically

aie::vector<float, 8> scale_factor = aie::broadcast<float, 8>( 0.0000152587890625 ); // 1 / 2^16
// perform fast exp algorithm on all 8 vector lanes
void fast_exp(aie::vector<float, 8>& exp)
{
    // Create a mask for values which could cause numerical underflow (avoid NaN for very small values)
    // Since e^-88 is near the underflow threshold for 32b floats, we mask out any exponents less than -88
    aie::mask<8> underflow_mask = aie::lt(exp, aie::broadcast<float, 8>(-30.0f)); // was -87.0
    
    // apply factor to scale exponent
    exp = aie::mul(exp, scale_factor);

    exp = aie::add(exp, aie::broadcast<float, 8>( 1.0 )); // old method, this can result in underflow, returning NaN, for very negative exponents

    for(int i = 0; i < 16; i++) // repeated squaring T times yields approximatly e^x
      exp = aie::mul(exp, exp);

    // filter any NaN results
    exp = aie::select(
                exp,
                //aie::broadcast<float, 8>(__FLT_MIN__),    // Zero out values which are masked
                aie::broadcast<float, 8>(0),    // Zero out values which are masked
                underflow_mask);                   // Mask

}

void partials_kernel( input_stream<float> * __restrict x_in, output_stream<float> * __restrict partials_out)
{
	// Read control data
	aie::vector<float, 8> ctrl;
	ctrl = readincr_v<8>(x_in);
	int32 net_size  = ctrl.get(0);
	int32 net_groups = ctrl.get(1);
	// ctrl(2) thru ctrl(7) unused

	aie::vector<float, 8> x_vals[MAX_NET_SIZE], a_plus[MAX_NET_SIZE], a_minus[MAX_NET_SIZE]; // Max net size kernel must handle is 8
	aie::vector<float, 8> b_plus, b_minus;
	aie::vector<float, 8> data, b_squared_inv; // intermediate results
	aie::accum<accfloat, 8> c_plus, c_minus;
	aie::accum<accfloat, 8> plus_term, minus_term;

	// For parallelization, each net group contains 8 nets of order net_size
	for(int i = 0; i < net_groups; i++) {
		x_vals[0] = readincr_v<8>(x_in); // first 8 vals are always the max for these nets(pre-sorted)
		// **************
		// Process term 0
		// a+ for max val is simply e^0 = 1.0
		//writeincr(xa_out, max_vals);  // Stream out x_0
		// TODO: always sending ones! opportunity to reduce communication
		//################################
		
		//writeincr(xa_out, ones);      // Stream out a_plus_0

		x_vals[1] = readincr_v<8>(x_in); // second 8 vals are always the min for these nets
		// compute a- for max val
		data = aie::sub(x_vals[1], x_vals[0]); // x_min - x_max
		data = aie::mul((float)inv_gamma, data); // compute (x_min - x_max) / gamma
		fast_exp(data); // estimate e^(data)
		a_plus[0] = aie::broadcast<float, 8>( 1.0 );
		a_minus[0] = data;
		//writeincr(xa_out, data);    // Stream out a_minus_0

		// begin cumulative total for b+ and c+
		b_plus = aie::broadcast<float, 8>( 1.0 ); // Since a_plus_0 is always 1, init b_plus to ones
		c_plus.from_vector(x_vals[0]); // init c_plus to 1*max_vals

		// begin cumulative total for b- and c-
		b_minus = data; // init b_minus to computed a_minus
		//c_minus.from_vector(aie::broadcast<float, 8>( 0.0 )); 
		c_minus = aie::mul(x_vals[0], data);// init c_minus to x0 * a_minus

		// **************
		// Process term 1
		//writeincr(xa_out, min_vals);  // Stream out x_1
		// a- (max val) is always the same as a+ (min val) 
		// TODO: Redundant data! opportunity to reduce communication
		//writeincr(xa_out, data);    // Stream out a_plus_1 (always same as a_minus_0)
		a_plus[1] = data;

		// a- for min val is simply e^0 = 1.0
		//################################
		//writeincr(xa_out, ones);    // Stream out a_minus_1 
		a_minus[1] = aie::broadcast<float, 8>( 1.0 );

		// add a+ to cumulative total for b+ and c+
		b_plus = aie::add(b_plus, data); // b_plus += a_plus_1
		c_plus = aie::mac(c_plus, x_vals[1], data); // c_plus += x_1 * a_plus_1

		// add a- to cumulative total for b- and c-
		b_minus = aie::add(b_minus, aie::broadcast<float, 8>( 1.0 )); // b_minus += a_minus_1
		c_minus = aie::mac(c_minus, x_vals[1], aie::broadcast<float, 8>( 1.0 )); // c_minus += x_1 * a_minus_1
    


		// if net_size is 3 or greater, compute terms up to netsize
		for (int i = 2; i < net_size; i++) {
			x_vals[i] = readincr_v<8>(x_in);
			//writeincr(xa_out, x_vals);    // Stream out x_i

			// Compute a+
			data = aie::sub(x_vals[i], x_vals[0]);
			data = aie::mul((float)inv_gamma, data); // compute (x - x_max) / gamma
			fast_exp(data); // estimate e^(data)

			//writeincr(xa_out, data);    // Stream out a_plus_i
			a_plus[i] = data;
			// add a+ to cumulative total for b+ and c+
			b_plus = aie::add(b_plus, data); // b_plus += a_plus_i
			c_plus = aie::mac(c_plus, x_vals[i], data); // c_plus += x_i * a_plus_i


			// Compute a-
			data = aie::sub(x_vals[1], x_vals[i]);
			data = aie::mul((float)inv_gamma, data); // compute (x_min - x) / gamma
			fast_exp(data); // estimate e^(data)
			//writeincr(xa_out, data);    // Stream out a_minus_i 
			a_minus[i] = data;

			// add a- to cumulative total for b- and c-
			b_minus = aie::add(b_minus, data); // b_minus += a_minus_i
			c_minus = aie::mac(c_minus, x_vals[i], data); // c_minus += x_i * a_minus_i

		}

		

		//for(int n = 0; n < net_size; n++) {
		//	writeincr(partials_out, x_vals[n]);
		//}

		// compute partials for each x val on these nets
		for(int n = 0; n < net_size; n++) {
			// compute the plus term
			//plus_term.from_vector(ones, 0);
			//plus_term = aie::mac(plus_term, inv_gamma, x_vals);			// (1 + x/gamma)
			//plus_term = aie::mul(plus_term.to_vector<float>(0), b_plus);	// (1 + x/gamma)*b+
			//plus_term = aie::msc(plus_term, inv_gamma, c_plus); 		// [((1 + x/gamma)*b+) - (c+ / gamma)]
			//b_squared_inv = aie::mul_square(aie::inv(b_plus));				// b+^-2
			//plus_term = aie::mul(plus_term.to_vector<float>(0), b_squared_inv);	// [((1 + x/gamma)*b+) - (c+ / gamma)] / b+^-2
			//plus_term = aie::mul(plus_term.to_vector<float>(0), a_plus);   	// a+ * [((1 + x/gamma)*b+) - (c+ / gamma)] / b+^2

			// DEBUGGING PRINTFs
			plus_term.from_vector(x_vals[n], 0);
			//printf("line: %d\tplus_term: %f\n", __LINE__, plus_ptr[0]);

			plus_term = aie::mul(plus_term.to_vector<float>(0), b_plus);	// x*b+
			//printf("line: %d\tplus_term: %f\n", __LINE__, plus_ptr[0]);

			plus_term = aie::sub(plus_term, c_plus.to_vector<float>(0));	// x*b+ - c+
			//printf("line: %d\tplus_term: %f\n", __LINE__, plus_ptr[0]);

			plus_term = aie::mul(plus_term.to_vector<float>(0), inv_gamma);	// (x*b+ - c+) / gamma
			//printf("line: %d\tplus_term: %f\n", __LINE__, plus_ptr[0]);

			//plus_term = aie::msc(plus_term, inv_gamma, c_plus); 		// [((1 + x/gamma)*b+) - (c+ / gamma)]
			plus_term = aie::add(plus_term, b_plus); // b+ + (x*b+ - c+) / gamma
			//printf("line: %d\tplus_term: %f\n", __LINE__, plus_ptr[0]);

			b_squared_inv = aie::mul_square(aie::inv(b_plus));	// PRECOMUTE THIS			// b+^-2
			plus_term = aie::mul(plus_term.to_vector<float>(0), b_squared_inv);	// [((1 + x/gamma)*b+) - (c+ / gamma)] / b+^-2
			//printf("line: %d\tplus_term: %f\n", __LINE__, plus_ptr[0]);

			plus_term = aie::mul(plus_term.to_vector<float>(0), a_plus[n]);   	// a+ * [((1 + x/gamma)*b+) - (c+ / gamma)] / b+^2
			//printf("line: %d\tplus_term: %f\n", __LINE__, plus_ptr[0]);

			//printf("%s: %s, %d\n", __FUNCTION__, X86SIM_KERNEL_NAME, __LINE__);

			// compute the minus term
			minus_term.from_vector(x_vals[n], 0);
			minus_term = aie::mul(minus_term.to_vector<float>(0), b_minus);
			//printf("line: %d\tminus_term: %f\n", __LINE__, minus_ptr[0]);
			minus_term = aie::sub(c_minus, minus_term.to_vector<float>(0));	// c- - x*b-
			//printf("line: %d\tminus_term: %f\n", __LINE__, minus_ptr[0]);
			minus_term = aie::mul(minus_term.to_vector<float>(0), inv_gamma); // (c- - x*b-) / gamma
			//printf("line: %d\tminus_term: %f\n", __LINE__, minus_ptr[0]);
			minus_term = aie::add(minus_term.to_vector<float>(0), b_minus); // b- + (c- - x*b-) / gamma
			//printf("line: %d\tminus_term: %f\n", __LINE__, minus_ptr[0]);

			b_squared_inv = aie::mul_square(aie::inv(b_minus));// PRECOMUTE THIS
			minus_term = aie::mul(minus_term.to_vector<float>(0), b_squared_inv); // (b- + (c- - x*b-) / gamma) / (b-)^2
			//printf("line: %d\tminus_term: %f\n", __LINE__, minus_ptr[0]);

			// subtract and write result
			//plus_term = aie::msc(plus_term, minus_term.to_vector<float>(0), a_minus); // partial = plus_term - term*(a-)
			minus_term = aie::mul(minus_term.to_vector<float>(0), a_minus[n]); // term * a-
			plus_term  = aie::sub(plus_term, minus_term.to_vector<float>(0)); // partial = plus_term - minus_term



			// TEMPORARY DEBUG
			//writeincr(partials_out, x_vals[n]);
			//writeincr(partials_out, a_plus[n]);
			//writeincr(partials_out, a_minus[n]);

			//writeincr(partials_out, b_plus);
			//writeincr(partials_out, b_minus);
			//writeincr(partials_out, c_plus.to_vector<float>(0));
			//writeincr(partials_out, c_minus.to_vector<float>(0));
		
			writeincr(partials_out, plus_term.to_vector<float>(0));
	  	}
	}
}
