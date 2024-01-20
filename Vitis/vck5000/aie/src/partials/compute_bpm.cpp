// compute_bpm.cpp
// AIEngine kernel to compute b+, b- for the ePlace algorithm
#include <adf.h>
#include "partials_kernels.h"

#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

// Inputs are a+, a- and x coord values. each lane is for a net, so 8 nets are processed simultaneously
//template<NETSIZE>
//For netsize 2 or 3, cannot use streams! use buffers of fixed size for inter-kernel comms
void compute_bpm(
    input_stream<float> * __restrict a_plus_in, input_stream<float> * __restrict a_minus_in,
    output_stream<float> * __restrict b_plus_out, output_stream<float> * __restrict b_minus_out)
{
	aie::vector<float, 8> a_plus, a_minus;
	aie::vector<float, 8> b_plus, b_minus;

	int32 net_size = readincr(a_plus_in); 
	int32 net_count = readincr(a_plus_in); // will be multiple of 8?

	for(int i = 0; i < net_count; i++)
	{
		// Initialize to zeros
		b_plus = aie::broadcast<float, 8>( 0.0 );
		b_minus = aie::broadcast<float, 8>( 0.0 );

		// Compute terms
		for(int n = 0; n < net_size; n++)
		{
			a_plus = readincr_v<8>(a_plus_in);
			a_minus = readincr_v<8>(a_minus_in);

			b_plus  = aie::add(b_plus , a_plus);
			b_minus = aie::add(b_minus, a_minus);
		}

		// Write output streams
		writeincr(b_plus_out, b_plus);
		writeincr(b_minus_out, b_minus);
	}
}
