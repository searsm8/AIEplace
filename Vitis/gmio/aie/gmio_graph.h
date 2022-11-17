#pragma once
#include "system_settings.h"
#include "kernels.h"

namespace transform = xf::dsp::aie::fft;

/* gmio_graph class is a template for any graph using GMIO
 * especially DCT, IDCT
 */
class gmio_graph : public adf::graph {
public:
    transform::dit_1ch::fft_ifft_dit_1ch_graph<FFT_DATA_TYPE, TWIDDLE_TYPE, POINT_SIZE, FFT_DIR, TP_SHIFT, TP_CASC_LEN, TP_DYN_PT_SIZE, TP_WINDOW_VSIZE> FFT_subgraph;
    input_gmio  gmio_in;
    output_gmio gmio_out;

};