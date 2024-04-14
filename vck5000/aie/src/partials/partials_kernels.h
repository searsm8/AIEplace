#ifndef PARTIALS_KERNELS_H
#define PARTIALS_KERNELS_H

#include "system_settings.h"
using namespace adf;

#define inv_gamma 0.25 // 1/gamma factor for exponents (This should ideally be a runtime parameter set by the graph, but can't do that on VCK5000)

// ********************
// HPWL compute kernels
// ********************

#define BUFF_SIZE 64*4 // 64*4 bytes = 64 floats
                       // for one kernel execution, this buffer size is big enough 
                       // to hold up to 8 nets of size 8
                       // or 16 nets of size 4, or 32 nets of size 2
                       // or 8 nets of size 6 and 8 nets of size 2, etc.
                       // net size and count is sent via the ctrl stream
        // for nets larger than size 8, must be computed elsewhere (perhaps in host? or PL?)
        // 64 floats is the smallest feesible buffer size to handle nets of size 8
        // If larger nets need to be computed, can increase this buffer size
        // If net_size * net_count < 64, need to pad with 0's to create a full 64 float window

// Stream only kernels
void compute_abc(input_stream<float> * __restrict x_in, output_stream<float> * __restrict xa_out, output_stream<float> * __restrict bc_out);
void compute_partials( input_stream<float> * __restrict xa_in, input_stream<float> * __restrict bc_in, output_stream<float> * __restrict partials_out);


// Window based kernels
// a plus/minus
void compute_apm(input_stream <float> * __restrict ctrl, input_window<float> * __restrict x_in, output_window<float> * __restrict a_plus_out, output_window<float> * __restrict a_minus_out);
  

// b plus/minus
//void compute_bcpm( input_stream<float> * __restrict a_plus_in, input_stream<float> * __restrict a_minus_in,
//    output_stream<float> * __restrict b_plus_out, output_stream<float> * __restrict b_minus_out);

// c plus
//void compute_cp( input_stream<float> * __restrict x_in, input_stream<float> * __restrict a_plus_in, output_stream<float> * __restrict c_plus_out);

// c minus
//void compute_cm( input_stream<float> * __restrict x_in, input_stream<float> * __restrict a_minus_in, output_stream<float> * __restrict c_minus_out);

#endif
