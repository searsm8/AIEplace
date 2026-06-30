// TopGraph.cpp -- pl_algo AIE top. The shared aie/Makefile always compiles a file
// named TopGraph.cpp from aie/src/$(AIE)/src into libadf.
//
// HPWL runs entirely on the PL (the HpwlGradGraph here is PARKED -- kept in the tree
// but not instantiated). The active AIE graph is the density-field FFT pool: the AIE
// does only the forward FFT; all DCT/IDCT/IDXST pre/post runs on the PL.
#include "density_fft/DensityFFTGraph.h"

DensityFFTGraph density_fft_graph;

int main(void) {
  adf::return_code ret;

  density_fft_graph.init();
  ret = density_fft_graph.run(1);
  if (ret != adf::ok) {
    printf("DensityFFTGraph run failed\n");
    return ret;
  }
  ret = density_fft_graph.end();
  if (ret != adf::ok) {
    printf("DensityFFTGraph end failed\n");
    return ret;
  }
  return 0;
}
