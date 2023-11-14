// compute_apm.cpp
// AIEngine kernel to compute a_plus and a_minus for the ePlace algorithm

#include "compute_a.h"
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

void compute_apm(input_stream<float> * x_in, output_stream<float> * a_plus_out, output_stream<float> * a_minus_out)
{
  int32 net_size = readincr(x_in); 
  int32 net_count = readincr(x_in); // will be multiple of 8?

	aie::vector<float, 8> max_vals, min_vals;

	aie::vector<float, 8> factor = aie::broadcast<float, 8>( 0.0000152587890625 ); // 1 / 2^16
	aie::vector<float, 8> ones   = aie::broadcast<float, 8>( 1.0 );
	aie::vector<float, 8> data, temp;

  for (int net_idx = 0; net_idx < net_count/8; net_idx++) {
    max_vals = readincr_v<8>(x_in); // first 8 vals are always the max for these nets(pre-sorted)
    min_vals = readincr_v<8>(x_in); // second 8 vals are always the min for these nets

    // a+ for max val is simply e^0 = 1.0
    writeincr(a_plus_out, ones);

    // compute a+ for min val
    data = aie::sub(min_vals, max_vals);
    data = aie::mul((float)inv_gamma, data); // compute (x_min - x_max) / gamma
    fast_exp(data, factor, ones); // estimate e^(data)
    writeincr(a_plus_out, data);

    // This is always the same as a- for max val
    writeincr(a_minus_out, data);

    // a- for min val is simply e^0 = 1.0
    writeincr(a_minus_out, ones);
    
    // if net_size is 3 or greater, compute a^pm for other x values
    for (int i = 2; i < net_size; i++) {
      temp = readincr_v<8>(x_in);

      // Compute a+
      data = aie::sub(temp, max_vals);
      data = aie::mul((float)inv_gamma, data); // compute (x - x_max) / gamma
      fast_exp(data, factor, ones); // estimate e^(data)
      writeincr(a_plus_out, data);

      // Compute a-
      data = aie::sub(min_vals, temp);
      data = aie::mul((float)inv_gamma, data); // compute (x_min - x) / gamma
      fast_exp(data, factor, ones); // estimate e^(data)
      writeincr(a_minus_out, data);
    }
  }
}
