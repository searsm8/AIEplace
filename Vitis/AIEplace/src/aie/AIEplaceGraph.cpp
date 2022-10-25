
#include "AIEplaceGraph.h"

using namespace adf;

AIEplaceGraph my_graph;

int main(void) {
	my_graph.init();
	my_graph.run(NUM_ITER);
	my_graph.end();
  return 0;
}
