#include "DCTGraph.h"

DCTGraph density_graph;

int main(void) {
  adf::return_code ret;
  density_graph.init();
  ret=density_graph.run(POINT_SIZE);
  if(ret!=adf::ok){
    printf("Run failed\n");
    return ret;
  }
  ret=density_graph.end();
  if(ret!=adf::ok){
    printf("End failed\n");
    return ret;
  }
  return 0;
}
