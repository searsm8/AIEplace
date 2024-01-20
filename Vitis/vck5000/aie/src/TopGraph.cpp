#include "partials/PartialsGraph.h"
#include "density/DCTGraph.h"

PartialsGraph partials_graph;
DCTGraph dct_graph;
//IDCTgraph idct_graph;
//IDXSTgraph idxst_graph;

int main(void) {
  adf::return_code ret;
  
  // Run partials graph
  partials_graph.init();
  ret = partials_graph.run(3);

  if(ret != adf::ok){
    printf("PartialsGraph run failed\n");
    return ret;
  }

  ret = partials_graph.end();

  if(ret != adf::ok){
    printf("PartialsGraph end failed\n");
    return ret;
  }

  // Run DCT graph
  dct_graph.init();
  ret = dct_graph.run(POINT_SIZE); 

  if(ret != adf::ok){
    printf("DCTgraph run failed\n");
    return ret;
  }

  ret = dct_graph.end();

  if(ret != adf::ok){
    printf("DCTgraph end failed\n");
    return ret;
  }

  return 0;
}
