#include "graph.h"

simpleGraph mygraph;

int main(void) {
  adf::return_code ret;
  mygraph.init();
  ret=mygraph.run(1);
  if(ret!=adf::ok){
    printf("Run failed\n");
    return ret;
  }
  ret=mygraph.end();
  if(ret!=adf::ok){
    printf("End failed\n");
    return ret;
  }
  return 0;
}
