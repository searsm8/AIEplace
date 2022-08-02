// compute_b.cpp
// kernel for computing b+ = sum a+
#include <adf.h>
#include "include.h"
#include "kernels.h"

//#include "aie_api/aie.hpp"
//#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp> // what is this needed for?

// kernel to compute b+ and b- terms
// inputs are a+ or a- values. each lane is for a net, so 8 nets are processed simultaneously
template <int N>
void compute_b(input_window_float * in, output_window_float * out) {
	aie::accum<accfloat, 8> acc;
	acc.from_vector(aie::zeros<float, 8>(), 0);
	for(int n = 0; n < N; n++)
		acc = aie::mac(acc, window_readincr_v<8>(in), (float)1.0);
	window_writeincr(out, acc.to_vector<float>(0));
}
