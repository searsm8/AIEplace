#include <adf.h>
#include "kernels.h"
#include <aie_api/utils.hpp>
//#include "system_settings.h"

void idxst_preprocess(input_window<FFT_DATA_TYPE> * in, output_window<FFT_DATA_TYPE> * out) {
  /* EXPECTED INPUT
   * 
   */

	aie::vector<float, 8> factor = aie::broadcast<float, 8>( 0.0000152587890625 ); // 1 / 2^16
	aie::vector<FFT_DATA_TYPE, 8> data;

	float k_init[8]={0,1,2,3,4,5,6,7};
	aie::vector<float, 8> k = aie::load_v<8>(k_init); 
	aie::vector<float, 8> eights = aie::broadcast<float, 8>( 8.0 );
	aie::vector<float, 8> alpha;
	aie::vector<float, 8> alpha_base = aie::broadcast<float, 8>( -PI/2/POINT_SIZE);

	aie::vector<cfloat, 8> adjust_factor;

	/* perform IDXST shuffle
	*  Flip order, but preserve first element
	*  Input: a b c d e f g h
	* Output: a h g f e d c b
	*/

	// could be optimized using aie::reverse???
	data[0] = window_read(in)/2; // first element should be divided by 2. why?
	window_incr(in, POINT_SIZE-1);
	for(int i = 1; i < 8; i++) {
		data[i] = window_read(in);
		window_decr(in, 1);
	}

	for(int n = 0; n < POINT_SIZE/8; n++) {

		// perform IDXST preprocess
		// multiply each element by e ^ -pi*k / 2N
		alpha = aie::mul(alpha_base, k);
		adjust_factor = aie::sincos_complex(alpha); // Equivalent to e^(i*alpha)
		data = aie::mul(window_readincr_v<8>(in), adjust_factor);

        // Divide the first output by 2
        //if(n == 0)
        //    data[0] = data[0] * 0.5;

		window_writeincr(out, data);
		k = aie::add(k, eights);

		// read in next 8 cints to data
	for(int i = 1; i < 8; i++) {
		data[i] = window_read(in);
		window_decr(in, 1);
	}
	}
}
