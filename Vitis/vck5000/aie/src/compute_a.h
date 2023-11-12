#ifndef FUNCTION_KERNELS_H
#define FUNCTION_KERNELS_H

#include "aie_api/aie.hpp"
#include "aie_api/aie_adf.hpp"
#include <aie_api/utils.hpp>

void compute_a(input_stream<float>*, output_stream<float>*, output_stream<float>*);

#endif
