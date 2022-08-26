
#include "dct_graph.h"

using namespace adf;

dct_subsystem my_dct_graph;

int main(void) {
	my_dct_graph.init();
	my_dct_graph.run(NUM_ITER);
	my_dct_graph.end();
  return 0;
}
