#pragma once

#include <adf.h>
#include "fft_ifft_dit_1ch_graph.hpp"
#include "kernels.h"


#define FFT_DIR 1 // 0 for ifft, 1 for fft
#define TP_SHIFT 0
#define TP_CASC_LEN 1
#define TP_DYN_PT_SIZE 0
#define TP_WINDOW_VSIZE POINT_SIZE

#define NUM_ITER POINT_SIZE
