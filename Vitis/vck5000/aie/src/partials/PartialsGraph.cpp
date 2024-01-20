#include "PartialsGraph.h"

PartialsGraph partials_graph;

int main(void) {
  adf::return_code ret;
  partials_graph.init();
  ret=partials_graph.run(1);
  if(ret!=adf::ok){
    printf("Run failed\n");
    return ret;
  }
  ret=partials_graph.end();
  if(ret!=adf::ok){
    printf("End failed\n");
    return ret;
  }
  return 0;
}
