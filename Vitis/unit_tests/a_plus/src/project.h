
#include <adf.h>
#include "kernels.h"

using namespace adf;

class simpleGraph : public adf::graph {
private:
  kernel k1;
public:
  input_plio  in0, in1;
  output_plio out;
  simpleGraph(){
    
    in0  = input_plio::create(plio_32_bits, "data/input0.txt");
    out = output_plio::create(plio_32_bits, "data/output.txt");

    k1 = kernel::create(a_plus);
    connect< window<32> > net0 (in0.out[0], k1.in[0]);
    connect< window<32> > net_out (k1.out[0], out.in[0]);

    source(k1) = "kernels/kernels.cc";

    runtime<ratio>(k1) = 0.1;

  }
};
