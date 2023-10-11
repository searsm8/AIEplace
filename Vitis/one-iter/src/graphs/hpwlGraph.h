
#include <adf.h>
#include "kernels.h"

using namespace adf;
//#define DEBUG_OUTPUT // output files for a+, a-, b+, b-, c+, c-

class hpwlGraph : public adf::graph {
private:
	kernel a_kernel;  //kernel for a+ and a-
	kernel bc_kernel; // kernel for b+, b-, c+ and c-
	kernel hpwl_kernel;
	kernel partials_kernel;
public:
  //port<input> netsize_a;
  //port<input> netsize_bc;
  //port<input> netsize_partials;

  port<input> in_data;
  port<input> net_size;
  port<output> out_HPWL;
  port<output> out_partials;

#ifdef DEBUG_OUTPUT
  output_plio out_a_plus, out_a_minus;
  output_plio out_b_plus, out_b_minus;
  output_plio out_c_plus, out_c_minus;
#endif
  
  hpwlGraph(){
#ifdef DEBUG_OUTPUT
    out_a_plus  = output_plio::create(plio_32_bits, "data/a_plus.dat");
    out_a_minus = output_plio::create(plio_32_bits, "data/a_minus.dat");
    out_b_plus  = output_plio::create(plio_32_bits, "data/b_plus.dat");
    out_b_minus = output_plio::create(plio_32_bits, "data/b_minus.dat");
    out_c_plus  = output_plio::create(plio_32_bits, "data/c_plus.dat");
    out_c_minus = output_plio::create(plio_32_bits, "data/c_minus.dat");
#endif

    // Create Kernels
    a_kernel = kernel::create(compute_a);
    bc_kernel = kernel::create(compute_bc);
    hpwl_kernel = kernel::create(compute_HPWL);
    partials_kernel = kernel::create(compute_partials);

    // connect netsize Runtime Parameter
    // Using parameters and stream inputs might be able to create a kernel
    // that can process any netsize!
    //connect<parameter>(netsize, async(a_kernel.in[1]));
    //connect<parameter>(netsize, async(bc_kernel.in[3]));
    //connect<parameter>(netsize, async(partials_kernel.in[7]));

    // Connect windows between kernels
    connect< window<32*NETSIZE> > net_a_kernel_in (in_data, a_kernel.in[0]); // a window of 32B contains eight 4B floats
    connect< window<32*NETSIZE> > net_bc_kernel_in_a_plus  (a_kernel.out[0], bc_kernel.in[0]);
    connect< window<32*NETSIZE> > net_bc_kernel_in_a_minus (a_kernel.out[1], bc_kernel.in[1]);
    connect< window<32*NETSIZE> > net_bc_kernel_inx (in_data, bc_kernel.in[2]);

    // connect HPWL kernel
    //for(int i = 0; i < 4; i++)
    //  connect< window<32*1> > (bc_kernel.out[i], hpwl_kernel.in[i]);
    connect< window<32*1> >       net_hpwl_b_plus (bc_kernel.out[0], hpwl_kernel.in[0]);
    connect< window<32*1> >       net_hpwl_b_minus(bc_kernel.out[1], hpwl_kernel.in[1]);
    connect< window<32*1> >       net_hpwl_c_plus (bc_kernel.out[2], hpwl_kernel.in[2]);
    connect< window<32*1> >       net_hpwl_c_minus(bc_kernel.out[3], hpwl_kernel.in[3]);

    // connect partials kernel
    //for(int i = 0; i < 4; i++)
    //  connect< window<32*1> > (bc_kernel.out[i], partials_kernel.in[i]);
    connect< window<32*1> > 		net_partials_b_plus (bc_kernel.out[0], partials_kernel.in[2]);
    connect< window<32*1> >       	net_partials_b_minus(bc_kernel.out[1], partials_kernel.in[3]);
    connect< window<32*1> >     	net_partials_c_plnus(bc_kernel.out[2], partials_kernel.in[4]);
    connect< window<32*1> > 		net_partials_c_minus(bc_kernel.out[3], partials_kernel.in[5]);
    connect< window<32*NETSIZE> >   net_partials_a_plus (a_kernel.out[0],  partials_kernel.in[0]);
    connect< window<32*NETSIZE> >   net_partials_a_minus(a_kernel.out[1],  partials_kernel.in[1]);
    connect< window<32*NETSIZE> >	net_partials_x 	  (in_data, partials_kernel.in[6]);


    // connect outputs
    connect< window<32*1> >       net_hpwl_out    (hpwl_kernel.out[0], out_HPWL);
    connect< window<32*NETSIZE> > net_partials_out(partials_kernel.out[0], out_partials);
#ifdef DEBUG_OUTPUT
    connect< window<32*NETSIZE> > net_a_plus_out  (a_kernel.out[0], out_a_plus.in[0]);
    connect< window<32*NETSIZE> > net_a_minus_out (a_kernel.out[1], out_a_minus.in[0]);
    connect< window<32*1> >       net_b_plus_out  (bc_kernel.out[0], out_b_plus.in[0]);
    connect< window<32*1> >       net_b_minus_out (bc_kernel.out[1], out_b_minus.in[0]);
    connect< window<32*1> >       net_c_plus_out  (bc_kernel.out[2], out_c_plus.in[0]);
    connect< window<32*1> >       net_c_minus_out (bc_kernel.out[3], out_c_minus.in[0]);
#endif

    // Source kernels
    source(a_kernel) = "kernels/compute_a.cpp";
    source(bc_kernel) = "kernels/compute_bc.cpp";
    source(hpwl_kernel) = "kernels/compute_HPWL.cpp";
    source(partials_kernel) = "kernels/compute_partials.cpp";

    // Set runtime ratios
    runtime<ratio>(a_kernel) = 1;
    runtime<ratio>(bc_kernel) = 1;
    runtime<ratio>(hpwl_kernel) = 1;
    runtime<ratio>(partials_kernel) = 1;
  }
};
