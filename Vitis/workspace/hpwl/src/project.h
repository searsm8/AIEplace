
#include <adf.h>
#include "kernels.h"

using namespace adf;
#define netsize 3
//#define DEBUG_OUTPUT // output files for a+, a-, b+, b-, c+, c-

class simpleGraph : public adf::graph {
private:
	kernel ka;  //kernel for a+ and a-
	kernel kbc; // kernel for b+, b-, c+ and c-
	kernel kHPWL;
	kernel kpartials;
public:
  input_plio  in_data;
  output_plio out_HPWL, out_partials;

#ifdef DEBUG_OUTPUT
  output_plio out_a_plus, out_a_minus;
  output_plio out_b_plus, out_b_minus;
  output_plio out_c_plus, out_c_minus;
#endif


  simpleGraph(){
    
	// Create IO files
	in_data   = input_plio::create(plio_32_bits, "data/input.dat");
    out_HPWL    = output_plio::create(plio_32_bits, "data/HPWL.dat");
    out_partials    = output_plio::create(plio_32_bits, "data/partials.dat");

#ifdef DEBUG_OUTPUT
	out_a_plus  = output_plio::create(plio_32_bits, "data/a_plus.dat");
    out_a_minus = output_plio::create(plio_32_bits, "data/a_minus.dat");
    out_b_plus  = output_plio::create(plio_32_bits, "data/b_plus.dat");
    out_b_minus = output_plio::create(plio_32_bits, "data/b_minus.dat");
    out_c_plus  = output_plio::create(plio_32_bits, "data/c_plus.dat");
    out_c_minus = output_plio::create(plio_32_bits, "data/c_minus.dat");
#endif

    // Create Kernels
    ka = kernel::create(compute_a<netsize>);
    kbc = kernel::create(compute_bc<netsize>);
    kHPWL = kernel::create(compute_HPWL);
    kpartials = kernel::create(compute_partials<netsize>);


    // Connect windows between kernels
    connect< window<32*netsize> > net_ka_in (in_data.out[0], ka.in[0]); // a window of 32B contains eight 4B floats
    connect< window<32*netsize> > net_kbc_in_a_plus  (ka.out[0], kbc.in[0]);
    connect< window<32*netsize> > net_kbc_in_a_minus (ka.out[1], kbc.in[1]);
    connect< window<32*netsize> > net_kbc_inx (in_data.out[0], kbc.in[2]);

    /*
     * Using parameters and stream inputs might be able to create a kernel that can process any netsize!
    connect< parameter > (curr_netsize, ka0.in[1]);
    connect< parameter > (curr_netsize, ka1.in[1]);
    connect< parameter > (curr_netsize, kb0.in[1]);
    connect< parameter > (curr_netsize, kb1.in[1]);
    connect< parameter > (curr_netsize, kc0.in[2]);
    connect< parameter > (curr_netsize, kc1.in[2]);
	*/

    // connect HPWL kernel
    connect< window<32*1> >       net_hpwl_b_plus (kbc.out[0], kHPWL.in[0]);
    connect< window<32*1> >       net_hpwl_b_minus(kbc.out[1], kHPWL.in[1]);
    connect< window<32*1> >       net_hpwl_c_plus (kbc.out[2], kHPWL.in[2]);
    connect< window<32*1> >       net_hpwl_c_minus(kbc.out[3], kHPWL.in[3]);

    // connect partials kernel
    connect< window<32*netsize> >   net_partials_a_plus (ka.out[0],  kpartials.in[0]);
    connect< window<32*netsize> >   net_partials_a_minus(ka.out[1],  kpartials.in[1]);
    connect< window<32*1> > 		net_partials_b_plus (kbc.out[0], kpartials.in[2]);
    connect< window<32*1> >       	net_partials_b_minus(kbc.out[1], kpartials.in[3]);
    connect< window<32*1> >     	net_partials_c_plnus(kbc.out[2], kpartials.in[4]);
    connect< window<32*1> > 		net_partials_c_minus(kbc.out[3], kpartials.in[5]);
    connect< window<32*netsize> >	net_partials_x 	  (in_data.out[0], kpartials.in[6]);


    // connect outputs to out files
    connect< window<32*1> >       net_hpwl_out    (kHPWL.out[0], out_HPWL.in[0]);
    connect< window<32*netsize> > net_partials_out(kpartials.out[0], out_partials.in[0]);
#ifdef DEBUG_OUTPUT
    connect< window<32*netsize> > net_a_plus_out  (ka.out[0], out_a_plus.in[0]);
    connect< window<32*netsize> > net_a_minus_out (ka.out[1], out_a_minus.in[0]);
    connect< window<32*1> >       net_b_plus_out  (kbc.out[0], out_b_plus.in[0]);
    connect< window<32*1> >       net_b_minus_out (kbc.out[1], out_b_minus.in[0]);
    connect< window<32*1> >       net_c_plus_out  (kbc.out[2], out_c_plus.in[0]);
    connect< window<32*1> >       net_c_minus_out (kbc.out[3], out_c_minus.in[0]);
#endif

    // Source kernels
    source(ka) = "kernels/compute_a.cpp";
    source(kbc) = "kernels/compute_bc.cpp";
    source(kHPWL) = "kernels/compute_HPWL.cpp";
    source(kpartials) = "kernels/compute_partials.cpp";

    // Set runtime ratios
    runtime<ratio>(ka) = 1;
    runtime<ratio>(kbc) = 1;
    runtime<ratio>(kHPWL) = 1;
    runtime<ratio>(kpartials) = 1;
  }
};
