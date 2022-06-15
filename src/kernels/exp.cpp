// Kernel to compute exponentials e^x
// This kernel probably won't be used in the final design, but is a useful test

#include <adf.h>
//#include "include.h"
#define ln_2 0.69314718

/*
void exp_2s(input_window_cint16 * in, output_window_cint16 * out) {
    cint 16 x;
    float result;
    window_readincr(in, x);
    // compute using: exp(x) = 2^(x/ln2)    
    exponent = x / ln_2;
    result = 2;
    result << exponent; 
    window_writeincr(out, result);
}

void exp_taylor(input_window_cint16 * in, output_window_cint16 * out) {
    cint 16 x;
    float sum
    window_readincr(in, x);
    // compute using: exp(x) = 1 + x/1! + x^2/2! + x^3/3! ...
    // compute using: exp(x) = 1 + x(1 + x/2(1 + x/3(1 + x/4(...))))
    float sum = 1.0f;
    for(int i = 9; i > 0; i--) //use the first 9 terms of Taylor series
        sum = 1 + x * sum / i;
    window_writeincr(out, sum);
}
*/

// TODO: write out kernels to compute ePlace such as xSUM(e^x)
// Get them ready to test on AMD computer

// TODO: Figure out how the PL is supposed to interface to AIE 
// (look at Xilinx/Vitis_Tutorials/LeNet)
