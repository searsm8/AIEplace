#ifndef KERNELS_H
#define KERNELS_H

#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

#define inv_gamma 0.25 // 1/gamma factor for exponents (This should probably be a runtime parameter set by the graph, but can't do that on VCK5000)
// HPWL compute kernels
void compute_abc(input_stream<float> * __restrict x_in, output_stream<float> * __restrict xa_out, output_stream<float> * __restrict bc_out);

// a plus/minus
//void compute_apm(input_stream<float> * __restrict x_in, output_stream<float> * __restrict a_plus_out, output_stream<float> * __restrict a_minus_out);

// b plus/minus
//void compute_bcpm( input_stream<float> * __restrict a_plus_in, input_stream<float> * __restrict a_minus_in,
//    output_stream<float> * __restrict b_plus_out, output_stream<float> * __restrict b_minus_out);

// c plus
//void compute_cp( input_stream<float> * __restrict x_in, input_stream<float> * __restrict a_plus_in, output_stream<float> * __restrict c_plus_out);

// c minus
//void compute_cm( input_stream<float> * __restrict x_in, input_stream<float> * __restrict a_minus_in, output_stream<float> * __restrict c_minus_out);

void compute_partials( input_stream<float> * __restrict xa_in, input_stream<float> * __restrict bc_in, output_stream<float> * __restrict partials_out);


// DCT kernels for Density computation

#endif
