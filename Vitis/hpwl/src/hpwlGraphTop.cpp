
#include "hpwlGraphTop.h"

using namespace adf;

hpwlGraphTop mygraph;

int main(void) {
  mygraph.init();
  //mygraph.update(mygraph.netsize_a, 3);
  //mygraph.update(mygraph.netsize_bc, 3);
  //mygraph.update(mygraph.netsize_partials, 3);
  mygraph.run(1000*1000); // process one set of 8 nets
  mygraph.end();
  return 0;
}
