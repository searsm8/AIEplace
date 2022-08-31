#include <adf.h>
#include "kernels.h"
#include <aie_api/utils.hpp>
//#include "system_settings.h"

void dct_postprocess(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out) {
  /* EXPECTED INPUT
   * 
   */

	aie::vector<float, 8> factor = aie::broadcast<float, 8>( 0.0000152587890625 ); // 1 / 2^16
	aie::vector<FFT_DATA_TYPE, 8> data;

	float k_init[8]={0,1,2,3,4,5,6,7};
	aie::vector<float,8> k = aie::load_v<8>(k_init); 
	aie::vector<float, 8> eights = aie::broadcast<float, 8>( 8.0 );
	aie::vector<float, 8> alpha;
	aie::vector<float, 8> alpha_base = aie::broadcast<float, 8>( -PI/2/POINT_SIZE);

	aie::vector<cfloat, 8> adjust_factor;
	aie::vector<float, 16> real_data;

	// compute e ^ -pi*k / 2N
	for(int n = 0; n < POINT_SIZE/8; n++) {
		alpha = aie::mul(alpha_base, k);
		adjust_factor = aie::sincos_complex(alpha); // Equivalent to e^(i*alpha)
		data = aie::mul(window_readincr_v<8>(in), adjust_factor);

		// Take only the real part
		real_data = data.cast_to<float>();
		//result = aie::filter_even(real_data);
		for(int i = 0; i < 8; i++)
			real_data[2*i+1] = 0; // Set imaginary part to zero
		data = real_data.cast_to<cfloat>();
		window_writeincr(out, data);
		k = aie::add(k, eights);
	}
}
