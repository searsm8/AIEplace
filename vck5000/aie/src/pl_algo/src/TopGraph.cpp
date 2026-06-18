// TopGraph.cpp -- pl_algo AIE top. Bring-up instantiates only the HPWL-gradient
// graph; the density_grad (FFT) graph will be added here later. The shared
// aie/Makefile always compiles a file named TopGraph.cpp from aie/src/$(AIE)/src.
#include "hpwl_grad/HpwlGradGraph.h"

HpwlGradGraph hpwl_grad_graph;

int main(void) {
  adf::return_code ret;

  hpwl_grad_graph.init();
  ret = hpwl_grad_graph.run(1);
  if (ret != adf::ok) {
    printf("HpwlGradGraph run failed\n");
    return ret;
  }
  ret = hpwl_grad_graph.end();
  if (ret != adf::ok) {
    printf("HpwlGradGraph end failed\n");
    return ret;
  }
  return 0;
}
