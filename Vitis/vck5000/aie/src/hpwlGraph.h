#include <adf.h>
#include "compute_apm.h"

//using namespace adf;

class hpwlGraph : public adf::graph {
private:
  adf::kernel apm_kernel; // a plus/minus
  adf::kernel bcpm_kernel; // b and c plus/minus
  //adf::kernel hpwl_kernel; // not needed!
  adf::kernel partials_kernel;
public:
  adf::input_plio x_in;
  adf::output_plio out_a_plus, out_a_minus;

  hpwlGraph(){
    apm_kernel = adf::kernel::create(compute_apm);

    x_in = adf::input_plio::create("x_in", adf::plio_32_bits, "data/golden/hpwl/input_nets.dat");
    //num_elements  = input_plio::create("numels", plio_32_bits, "data/num_elements.txt");
    out_a_plus = adf::output_plio::create("out_a_plus", adf::plio_32_bits, "data/output_a_plus.dat");
    out_a_minus = adf::output_plio::create("out_a_minus", adf::plio_32_bits, "data/output_a_minus.dat");

    adf::connect<adf::stream>(x_in.out[0], apm_kernel.in[0]);
    //adf::connect<stream>(num_elements.out[0], sum_kernel.in[1]);
    adf::connect<adf::stream>(apm_kernel.out[0], out_a_plus.in[0]);
    adf::connect<adf::stream>(apm_kernel.out[1], out_a_minus.in[0]);

    adf::source(apm_kernel) = "compute_apm.cpp";

    adf::runtime<ratio>(apm_kernel) = 0.9;
  }
};
