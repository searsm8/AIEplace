#include <adf.h>
#include "compute_a.h"

using namespace adf;

class simpleGraph : public graph {
private:
  kernel a_kernel;
public:
  input_plio in;
  output_plio out_a_plus, out_a_minus;

  simpleGraph(){
    a_kernel = kernel::create(compute_a);

    in = input_plio::create("x_in", plio_32_bits, "data/golden/hpwl/input_nets.dat");
    //num_elements  = input_plio::create("numels", plio_32_bits, "data/num_elements.txt");
    out_a_plus  = output_plio::create("out_a_plus", plio_32_bits, "data/output_a_plus.dat");
    out_a_minus = output_plio::create("out_a_minus", plio_32_bits, "data/output_a_minus.dat");

    connect<stream>(in.out[0], a_kernel.in[0]);
    //connect<stream>(num_elements.out[0], sum_kernel.in[1]);
    connect<stream>(a_kernel.out[0], out_a_plus.in[0]);
    connect<stream>(a_kernel.out[1], out_a_minus.in[0]);

    source(a_kernel) = "compute_a.cpp";

    runtime<ratio>(a_kernel) = 0.9;
  }
};
