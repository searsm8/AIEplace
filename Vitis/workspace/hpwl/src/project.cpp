
#include <adf.h>
#include "kernels.h"
#include "project.h"

using namespace adf;

simpleGraph mygraph;

int main(void) {
  mygraph.init();
  //mygraph.update(mygraph.curr_netsize, 3);
  mygraph.run(1); // process one set of 8 nets
  mygraph.end();
  return 0;
}
