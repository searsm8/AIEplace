#include <adf.h>
#include "density_kernels.h"
#include <aie_api/utils.hpp>
//#include "system_settings.h"

void dct_adjustment(
	input_window<FFT_DATA_TYPE> * in, 
	output_window<FFT_DATA_TYPE> * out
) {
  /* dct_postprocess performs vectorized post-processing of DCT computation
   * This kernel performs the computations of this equivalent Python code:
   *
    *for i in range(len(my_dct)):
    *    my_dct[i][0] *= 0.5 # why cut in half? boundary conditions?
    *for i in range(len(my_dct[0])):
    *    my_dct[0][i] *= 0.5
    *for i in range(len(my_dct)):
    *    for j in range(len(my_dct[0])):
    *        my_dct[i][j] *= 4.0 / len(my_dct) / len(my_dct[0])
*
    *# w_u and w_v factors
    *for u in range(len(my_dct)):
    *    for v in range(len(my_dct[0])):
    *        w_u = 1*math.pi*u / len(my_dct) # why not 2*pi?
    *        w_u2 = w_u*w_u
    *        w_v = 1*math.pi*v / len(my_dct[0])
    *        w_v2 = w_v*w_v
    *        if u == 0 and v == 0:
    *            electroPhi[u][v] = 0
    *        else:
    *            electroPhi[u][v] = my_dct[u][v] / (w_u2 + w_v2)
    *            electroForceX[u][v] = electroPhi[u][v] * w_u
    *            electroForceY[u][v] = electroPhi[u][v] * w_v
*
   * OUTPUT: DCT
   */

	aie::vector<FFT_DATA_TYPE, 8> data;
	float k_init[8]={0,1,2,3,4,5,6,7};
	aie::vector<float, 8> k = aie::load_v<8>( k_init ); 
	aie::vector<float, 8> eights = aie::broadcast<float, 8>( 8.0 );
	aie::vector<float, 8> alpha;
	aie::vector<float, 8> alpha_base = aie::broadcast<float, 8>( -PI / (2*POINT_SIZE) );
	float real_init[16] = {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0};
	aie::vector<float, 16> real_only = aie::load_v<16>( real_init );

	aie::vector<cfloat, 8> adjust_factor;
	aie::vector<float, 16> real_data;

	for(int i = 0; i < POINT_SIZE/8; i++) {
	// PRAGMAS?
		// compute alpha = -pi*k / 2N
		alpha = aie::mul(alpha_base, k);

		// compute e^(i*alpha) = cos(alpha) + i*sin(alpha)
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
