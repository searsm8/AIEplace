
#include <adf.h>
#include "TopGraph.h"

AIEplaceGraph aieplace;


#if defined(__AIESIM__) || defined(__X86SIM__) || defined(__ADF_FRONTEND__)

#define ITERATIONS 1

int main(void) {
  aieplace.init();

  //adf::event::handle handle = adf::event::start_profiling(mygraph.plio_out[0], adf::event::io_total_stream_running_to_idle_cycles);

  aieplace.run(ITERATIONS);
  aieplace.wait();

  //long long cycle_count = adf::event::read_profiling(handle);
  //adf::event::stop_profiling(handle);
  //double bandwidth = (double)(1024*ITERATIONS) / (cycle_count*1e-9); //Byte per second
  //std::cout<<"Bandwidth "<<std::dec<<bandwidth<<" B/s" << ", cycle_count: " << cycle_count<<std::endl;

  aieplace.end();

  return 0;
}
#endif
