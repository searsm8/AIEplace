#include "partials/PartialsGraph.h"
#include "density/DCTGraph.h"
#include "density/IDCTGraph.h"
#include "density/IDXSTGraph.h"

PartialsGraph partials_graph;
DCTGraph dct_graph;
IDCTGraph idct_graph;
IDXSTGraph idxst_graph;

int main(void) {
  adf::return_code ret;
  
  // Run partials graph
  partials_graph.init();
  ret = partials_graph.run(1);

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

  // Run IDCT graph
  idct_graph.init();
  ret = idct_graph.run(POINT_SIZE); 
  if(ret != adf::ok){
    printf("IDCTgraph run failed\n");
    return ret;
  }
  ret = idct_graph.end();
  if(ret != adf::ok){
    printf("IDCTgraph end failed\n");
    return ret;
  }

  // Run IDXST graph
  idxst_graph.init();
  ret = idxst_graph.run(POINT_SIZE); 
  if(ret != adf::ok){
    printf("IDXSTgraph run failed\n");
    return ret;
  }
  ret = idxst_graph.end();
  if(ret != adf::ok){
    printf("IDXSTgraph end failed\n");
    return ret;
  }
  return 0;
}
