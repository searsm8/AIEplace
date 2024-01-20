// PartialsGraphBuf.h
// implements a graph to compute hpwl partial derivatives using kernels WITH BUFFERS
#pragma once
#include "partials_kernels.h"

#define DEBUG_OUTPUT // will output files for a+, a-, b+, b-, c+, c-

class PartialsGraph : public adf::graph {
private:
  adf::kernel apm_kernel; // a plus/minus
  //adf::kernel bpm_kernel; // b plus/minus
  //adf::kernel cp_kernel;  // c plus
  //adf::kernel cm_kernel;  // c minus
  //adf::kernel hpwl_kernel; // not needed!
  //adf::kernel partials_kernel;
public:
  adf::input_plio ctrl;
  adf::input_plio x_in;

  PartialsGraph(){
    apm_kernel = adf::kernel::create(compute_apm);
    //bpm_kernel = adf::kernel::create(compute_bpm);

    // Primary inputs to the AIE array
    ctrl = input_plio::create("ctrl", adf::plio_32_bits, "golden_data/partials/ctrl.dat");
    x_in = input_plio::create("x_in", adf::plio_32_bits, "golden_data/partials/x_in.dat");

    // Input connections for apm_kernel
    connect< stream > control_data (ctrl.out[0], apm_kernel.in[0]); // control data
    connect< window<BUFF_SIZE> > coord_data (x_in.out[0], apm_kernel.in[1]); // x-coords

    // Input connections for bcpm_kernel
    //connect<adf::stream>(apm_kernel.out[0], bpm_kernel.in[0]); // a_plus
    //connect<adf::stream>(apm_kernel.out[1], bpm_kernel.in[1]); // a_minus

    // Input connections for partials_kernel

#ifdef DEBUG_OUTPUT
    // Optional outputs for debugging intermediate terms
    output_plio out_a_plus, out_a_minus;
    //output_plio out_b_plus, out_b_minus;
    //output_plio out_c_plus, out_c_minus;

    out_a_plus  = adf::output_plio::create("out_a_plus" , adf::plio_32_bits, "simdata/a_plus.dat");
    out_a_minus = adf::output_plio::create("out_a_minus", adf::plio_32_bits, "simdata/a_minus.dat");
    //out_b_plus  = adf::output_plio::create("out_b_plus" , adf::plio_32_bits, "simdata/b_plus.dat");
    //out_b_minus = adf::output_plio::create("out_b_minus", adf::plio_32_bits, "simdata/b_minus.dat");
    //out_c_plus  = adf::output_plio::create("out_c_plus" , adf::plio_32_bits, "simdata/c_plus.dat");
    //out_c_minus = adf::output_plio::create("out_c_minus", adf::plio_32_bits, "simdata/c_minus.dat");

    // Connections for debugging terms
    adf::connect< window<BUFF_SIZE> > out_ap (apm_kernel.out[0], out_a_plus.in[0]);
    adf::connect< window<BUFF_SIZE> > out_am (apm_kernel.out[1], out_a_minus.in[0]);
    //adf::connect<adf::stream>(bpm_kernel.out[0], out_b_plus.in[0]);
    //adf::connect<adf::stream>(bpm_kernel.out[1], out_b_minus.in[0]);
#endif


    adf::source(apm_kernel) = "compute_apm.cpp";
    adf::runtime<ratio>(apm_kernel) = 1;

    //adf::source(bpm_kernel) = "compute_bpm.cpp";
    //adf::runtime<ratio>(bpm_kernel) = 1;

  }
};
