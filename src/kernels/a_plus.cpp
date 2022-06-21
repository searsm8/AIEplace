// Kernel to compute the a+ term, which is a sum of exponentials e^x
// E.g. for a net with three x coordinates, compute e^x1 + e^x2 + e^x3

#include <adf.h>
#include <math.h> // is this supported in AIE? doubtful...
#include "include.h"

void a_plus(input_window_cint16 * in, output_window_cint16 * out)
{
    int n, max; // size of net
    window_readincr(in, n);
    window_writeincr(out, n); // feed through

    window_readincr(in, max); // max coord on net is always given first
    float a = 1; // a_plus for the max coord is always e^0 = 1
    window_writeincr(out, a);
    
    int coord;
    for(int i = 1; i < n; i++) {
        window_readincr(in, coord);
        a = exp((coord - max) / float(gamma)); // needs to call custom fast exp
        window_writeincr(out, a);
    }
}

