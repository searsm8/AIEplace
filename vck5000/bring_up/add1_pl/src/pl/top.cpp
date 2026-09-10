// top.cpp -- the entire PL "hello world" kernel: out[i] = in[i] + 1.
//
// Deliberately has NO AIE graph, NO AXIS stream ports, NO link.cfg
// connectivity -- just two DDR (m_axi) ports and a scalar arg over
// AXI4-Lite control. See ../../Makefile and ../../README.md for why:
// this exists to test the 2022.2-build / 2024.2-run xclbin-load question
// in isolation from dct_fft_aie's AIE CDO/PDI, per dct_fft_aie/HANDOFF.md.

extern "C" {
void add1(const float* in, float* out, int n) {
#pragma HLS INTERFACE m_axi port=in  offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=out offset=slave bundle=gmem1
#pragma HLS INTERFACE s_axilite port=in     bundle=control
#pragma HLS INTERFACE s_axilite port=out    bundle=control
#pragma HLS INTERFACE s_axilite port=n      bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    for (int i = 0; i < n; i++) {
#pragma HLS PIPELINE II=1
        out[i] = in[i] + 1.0f;
    }
}
} // extern "C"
