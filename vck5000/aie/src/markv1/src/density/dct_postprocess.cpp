#include "density_kernels.h"

// Stream Implementation
#ifdef USE_STREAM_IO
void dct_postprocess(
	input_stream <FFT_DATA_TYPE> * in, 
	output_stream<FFT_DATA_TYPE> * out
) {
  /* dct_postprocess performs vectorized post-processing of DCT computation
   * EXPECTED INPUT:
   * Ouput of FFT (with shuffled inputs)
   *
   * OUTPUT: DCT result
   */

	aie::vector<FFT_DATA_TYPE, 8> data;
	float k_init[8]={0,1,2,3,4,5,6,7};
	aie::vector<float, 8> k = aie::load_v<8>( k_init ); 
	aie::vector<float, 8> eights = aie::broadcast<float, 8>( 8.0 );
	aie::vector<float, 8> alpha;
	aie::vector<float, 8> alpha_base = aie::broadcast<float, 8>( -PI/2/POINT_SIZE );
	float real_init[16] = {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0};
	aie::vector<float, 16> real_only = aie::load_v<16>( real_init );

	aie::vector<cfloat, 8> adjust_factor;
	aie::vector<float, 16> real_data;

	for(int i = 0; i < POINT_SIZE/8; i++) {
	// PRAGMAS?
		// compute alpha = e ^ -pi*k / 2N
		alpha = aie::mul(alpha_base, k);

		// compute e^(i*alpha) = cos(alpha) + isin(alpha)
		adjust_factor = aie::sincos_complex(alpha);

		// compute FFT * e^(i*alpha)
		data = aie::mul(readincr_v<8>(in), adjust_factor);

		// DCT = Re[FFT * e^(i*alpha)]
		// Take only real part: multiply real parts by 1, imaginary parts by 0
		real_data = data.cast_to<float>();
		real_data = aie::mul(real_data, real_only);
		data = real_data.cast_to<cfloat>();
		writeincr(out, data);

		// increment k values for next vector of 8
		k = aie::add(k, eights);
	}
}

#else
// Window Implementation
void dct_postprocess(
	input_window<FFT_DATA_TYPE> * in, 
	output_window<FFT_DATA_TYPE> * out
) {
  /* dct_postprocess performs vectorized post-processing of DCT computation
   * EXPECTED INPUT:
   * Results of FFT (shuffled inputs)
   *
   * OUTPUT: DCT
   */

	aie::vector<FFT_DATA_TYPE, 8> data;
	float k_init[8]={0,1,2,3,4,5,6,7};
	aie::vector<float, 8> k = aie::load_v<8>( k_init ); 
	aie::vector<float, 8> eights = aie::broadcast<float, 8>( 8.0 );
	aie::vector<float, 8> alpha;
	aie::vector<float, 8> alpha_base = aie::broadcast<float, 8>( -PI/2/POINT_SIZE );
	float real_init[16] = {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0};
	aie::vector<float, 16> real_only = aie::load_v<16>( real_init );

	aie::vector<cfloat, 8> adjust_factor;
	aie::vector<float, 16> real_data;

	for(int i = 0; i < POINT_SIZE/8; i++) {
	// PRAGMAS?
		// compute alpha = e ^ -pi*k / 2N
		alpha = aie::mul(alpha_base, k);

		// compute e^(i*alpha) = cos(alpha) + isin(alpha)
		adjust_factor = aie::sincos_complex(alpha);

		// compute FFT * e^(i*alpha)
		data = aie::mul(window_readincr_v<8>(in), adjust_factor);

		// DCT = Re[FFT * e^(i*alpha)]
		// Take only real part: multiply real parts by 1, imaginary parts by 0
		real_data = data.cast_to<float>();
		real_data = aie::mul(real_data, real_only);
		data = real_data.cast_to<cfloat>();
		window_writeincr(out, data);

		// increment k values for next vector of 8
		k = aie::add(k, eights);
	}
}
#endif