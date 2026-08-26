// graph.cpp -- AIE top for the DCT-transpose <-> FFT integration test.
//
// Instantiates the 8-lane forward-FFT pool (DensityFFTGraph, verbatim from pl_algo). The AIE
// does ONLY the forward complex FFT; all DCT/IDCT/IDXST pre/post runs on the PL. Each lane is
// a window-API fft_ifft_dit_1ch (1024-pt, forward, TP_SHIFT=0). The host drives the graph with
// g.run(GRID / FFT_LANES) = g.run(128): one pass transforms all 1024 rows, 128 frames/lane.
//
// The global instance `dct_fft_graph` is what libadf exposes; the host looks it up by name via
// xrt::graph(device, uuid, "dct_fft_graph"). main() below is only for standalone x86/aiesim.

#include "DensityFFTGraph.h"

DensityFFTGraph dct_fft_graph;

#if defined(__AIESIM__) || defined(__X86SIM__)
int main(void) {
    dct_fft_graph.init();
    dct_fft_graph.run(1);      // one frame/lane for a standalone sim smoke test
    dct_fft_graph.end();
    return 0;
}
#endif
