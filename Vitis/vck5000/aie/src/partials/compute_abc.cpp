// compute_abc.cpp
// AIEngine kernel to compute a+/-, b+/-, and c+/- for the ePlace algorithm

#include "partials_kernels.h"
//#include "aie_api/aie.hpp"
//#include "aie_api/aie_adf.hpp"
//#include <aie_api/utils.hpp>

#define inv_gamma 0.25 // 1/gamma factor for exponents (This should probably be a runtime parameter set by the graph)

// perform fast exp algorithm on all 8 vector lanes
void fast_exp(aie::vector<float, 8>& data, aie::vector<float, 8>& factor, aie::vector<float, 8>& ones)
{
    data = aie::mul(data, factor);
    data = aie::add(data, ones); // data now contains: 1 + x/2^16
    for(int i = 0; i < 16; i++) // squaring 16 times yields sufficient precision?
      data = aie::mul(data, data);
}

// Output is streamed, but due to limitation of only two in or out streams per kernel, we split:
// Stream xa_out: x_0, a_plus_0, a_minus_0, x_1, a_plus_1, a_minus_1 ...
// Stream bc_out: NET_SIZE, NET_COUNT, b+, c+, b-, c-
void compute_abc(input_stream<float> * __restrict x_in, output_stream<float> * __restrict xa_out, output_stream<float> * __restrict bc_out)
{
  // Read control data (using vector because single floats mess up data streams for some reason)
	aie::vector<float, 4> ctrl = readincr_v4(x_in);
  int32 net_size  = ctrl.get(0);
  int32 net_count = ctrl.get(1); // will not always be multiple of 8
  // ignore ctrl(2) and ctrl(3)

  // Push control data onto output stream for use by partials kernel
  writeincr(bc_out, ctrl);

	aie::vector<float, 8> max_vals, min_vals;
	aie::vector<float, 8> a_plus, a_minus;
	aie::vector<float, 8> b_plus, b_minus;
	aie::accum<accfloat, 8> c_plus, c_minus;

	aie::vector<float, 8> factor = aie::broadcast<float, 8>( 0.0000152587890625 ); // 1 / 2^16
	aie::vector<float, 8> ones   = aie::broadcast<float, 8>( 1.0 );
	aie::vector<float, 8> data, x_vals;

  for (int net_idx = 0; net_idx < net_count/8; net_idx++) {
    max_vals = readincr_v<8>(x_in); // first 8 vals are always the max for these nets(pre-sorted)

    // **************
    // Process term 0
    // a+ for max val is simply e^0 = 1.0
    writeincr(xa_out, max_vals);  // Stream out x_0
    // TODO: always sending ones! opportunity to reduce communication
    writeincr(xa_out, ones);      // Stream out a_plus_0

    min_vals = readincr_v<8>(x_in); // second 8 vals are always the min for these nets
    // compute a- for max val
    data = aie::sub(min_vals, max_vals);
    data = aie::mul((float)inv_gamma, data); // compute (x_min - x_max) / gamma
    fast_exp(data, factor, ones); // estimate e^(data)
    writeincr(xa_out, data);    // Stream out a_minus_0

    // add a+ to cumulative total for b+ and c+
    b_plus = ones; // Since a_plus_0 is always 1, init b_plus to ones
    c_plus.from_vector(max_vals); // init c_plus to 1*max_vals

    // add a- to cumulative total for b- and c-
    b_minus = data; // init b_minus to computed a_minus
    c_minus.from_vector(aie::broadcast<float, 8>( 0 )); 
    c_minus = aie::mac(c_minus, max_vals, data);// init c_minus to x0 * a_minus

    // **************
    // Process term 1
    writeincr(xa_out, min_vals);  // Stream out x_1
    // a- (max val) is always the same as a+ (min val) 
    // TODO: Redundant data! opportunity to reduce communication
    writeincr(xa_out, data);    // Stream out a_plus_1 

    // a- for min val is simply e^0 = 1.0
    writeincr(xa_out, ones);    // Stream out a_minus_1 

    // add a+ to cumulative total for b+ and c+
    b_plus = aie::add(b_plus, data); // b_plus += a_plus_1
    c_plus = aie::mac(c_plus, min_vals, data); // c_plus += x_1 * a_plus_1

    // add a- to cumulative total for b- and c-
    b_minus = aie::add(b_minus, ones); // b_minus += a_minus_1
    c_minus = aie::mac(c_minus, min_vals, ones); // c_minus += x_1 * a_minus_1
    
    // **************
    // if net_size is 3 or greater, compute terms up to netsize
    for (int i = 2; i < net_size; i++) {
      x_vals = readincr_v<8>(x_in);
      writeincr(xa_out, x_vals);    // Stream out x_i

      // Compute a+
      data = aie::sub(x_vals, max_vals);
      data = aie::mul((float)inv_gamma, data); // compute (x - x_max) / gamma
      fast_exp(data, factor, ones); // estimate e^(data)
      // add a+ to cumulative total for b+ and c+
      b_plus = aie::add(b_plus, data); // b_plus += a_plus_i
      c_plus = aie::mac(c_plus, x_vals, data); // c_plus += x_i * a_plus_i

      writeincr(xa_out, data);    // Stream out a_plus_i
    

      // Compute a-
      data = aie::sub(min_vals, x_vals);
      data = aie::mul((float)inv_gamma, data); // compute (x_min - x) / gamma
      fast_exp(data, factor, ones); // estimate e^(data)
      // add a- to cumulative total for b- and c-
      b_minus = aie::add(b_minus, data); // b_minus += a_minus_i
      c_minus = aie::mac(c_minus, x_vals, data); // c_minus += x_i * a_minus_i

      writeincr(xa_out, data);    // Stream out a_minus_i 

    }

    // Stream out b and c terms
    writeincr(bc_out, b_plus);
    writeincr(bc_out, c_plus.to_vector<float>(0));
    writeincr(bc_out, b_minus);
    writeincr(bc_out, c_minus.to_vector<float>(0));
  }
}
