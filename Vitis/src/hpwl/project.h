
#include <adf.h>
#include "kernels.h"

using namespace adf;
#define netsize 3

class simpleGraph : public adf::graph {
private:
	kernel ka0, ka1;
	kernel kb0, kb1;
	kernel kc0, kc1;
public:
  input_plio  in_a_plus, in_a_minus;
  output_plio out_a_plus, out_a_minus;
  output_plio out_b_plus, out_b_minus;
  output_plio out_c_plus, out_c_minus;

  simpleGraph(){
    
	  // Create IO files
	  in_a_plus   = input_plio::create(plio_32_bits, "data/input_max.txt");
	  in_a_minus  = input_plio::create(plio_32_bits, "data/input_min.txt");
	  out_a_plus  = output_plio::create(plio_32_bits, "data/output_a_plus.txt");
    out_a_minus = output_plio::create(plio_32_bits, "data/output_a_minus.txt");
    out_b_plus  = output_plio::create(plio_32_bits, "data/output_b_plus.txt");
    out_b_minus = output_plio::create(plio_32_bits, "data/output_b_minus.txt");
    out_c_plus  = output_plio::create(plio_32_bits, "data/output_c_plus.txt");
    out_c_minus = output_plio::create(plio_32_bits, "data/output_c_minus.txt");

    // Create Kernels
    ka0 = kernel::create(compute_a<netsize>);
    ka1 = kernel::create(compute_a<netsize>);
    kb0 = kernel::create(compute_b<netsize>);
    kb1 = kernel::create(compute_b<netsize>);
    kc0 = kernel::create(compute_c<netsize>);
    kc1 = kernel::create(compute_c<netsize>);

    // Connect windows between kernels
    connect< window<32*netsize> > net_ka0 (in_a_plus.out[0], ka0.in[0]); // a window of 32B contains eight 4B floats
    connect< window<32*netsize> > net_kb0 (ka0.out[0], kb0.in[0]);
    connect< window<32*netsize> > net_ka1 (in_a_minus.out[0], ka1.in[0]);
    connect< window<32*netsize> > net_kb1 (ka1.out[0], kb1.in[0]);
    connect< window<32*netsize> > net_kc0_x (in_a_plus.out[0], kc0.in[0]);
    connect< window<32*netsize> > net_kc0_a (ka0.out[0], kc0.in[1]);
    connect< window<32*netsize> > net_kc1_x (in_a_minus.out[0], kc1.in[0]);
    connect< window<32*netsize> > net_kc1_a (ka1.out[0], kc1.in[1]);

    /*
     * Using parameters and stream inputs might be able to create a kernel that can process any netsize!
    connect< parameter > (curr_netsize, ka0.in[1]);
    connect< parameter > (curr_netsize, ka1.in[1]);
    connect< parameter > (curr_netsize, kb0.in[1]);
    connect< parameter > (curr_netsize, kb1.in[1]);
    connect< parameter > (curr_netsize, kc0.in[2]);
    connect< parameter > (curr_netsize, kc1.in[2]);
	*/

    connect< window<32*netsize> > net_a_plus_out  (ka0.out[0], out_a_plus.in[0]);
    connect< window<32*netsize> > net_a_minus_out (ka1.out[0], out_a_minus.in[0]);
    connect< window<32*1> >       net_b_plus_out  (kb0.out[0], out_b_plus.in[0]);
    connect< window<32*1> >       net_b_minus_out (kb1.out[0], out_b_minus.in[0]);
    connect< window<32*1> >       net_c_plus_out  (kc0.out[0], out_c_plus.in[0]);
    connect< window<32*1> >       net_c_minus_out  (kc1.out[0], out_c_minus.in[0]);

    // Source kernels
    source(ka0) = "kernels/compute_a.cpp";
    source(ka1) = "kernels/compute_a.cpp";
    source(kb0) = "kernels/compute_b.cpp";
    source(kb1) = "kernels/compute_b.cpp";
    source(kc0) = "kernels/compute_c.cpp";
    source(kc1) = "kernels/compute_c.cpp";

    // Set runtime ratios
    runtime<ratio>(ka0) = 1;
    runtime<ratio>(ka1) = 1;
    runtime<ratio>(kb0) = 1;
    runtime<ratio>(kb1) = 1;
    runtime<ratio>(kc0) = 1;
    runtime<ratio>(kc1) = 1;

  }
};
