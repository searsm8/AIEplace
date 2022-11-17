#include "dct_subgraph.h"
#include "idct_subgraph.h"
#include "idxst_subgraph.h"

class AIEplaceGraph: public adf::graph {
private:
  dct_subgraph DCT_subgraph[1];
  //idct_subgraph IDCT_subgraph[2];
  //idxst_subgraph IDXST_subgraph[2];

public:
  input_gmio  in, dct_2d_in;// idct_2d_in, idxst_2d_in;
  output_gmio dct_out,   dct_2d_out;
  //output_plio idct_out,  idct_2d_out;
  //output_plio idxst_out, idxst_2d_out;

  AIEplaceGraph(){

    in          = input_gmio::create("IN", 64, 1000);

    dct_out      = output_gmio::create("DCT_OUT", 64, 1000);
    //dct_2d_in   = input_plio::create("DCT_2D_IN", plio_32_bits, "data/dct_2d_input.dat");
    //dct_2d_out   = output_plio::create("DCT_2D_OUT", plio_32_bits, "data/dct_2d_output.dat");

    //idct_out     = output_plio::create("IDCT_OUT", plio_32_bits, "data/idct_output.dat");
    //idct_2d_in  = input_plio::create("IDCT_2D_IN", plio_32_bits, "data/idct_2d_input.dat");
    //idct_2d_out  = output_plio::create("IDCT_2D_OUT", plio_32_bits, "data/idct_2d_output.dat");

    //idxst_out    = output_plio::create("IDXST_OUT", plio_32_bits, "data/idxst_output.dat");
    //idxst_2d_in = input_plio::create("IDXST_2D_IN", plio_32_bits, "data/idxst_2d_input.dat");
    //idxst_2d_out = output_plio::create("IDXST_2D_OUT", plio_32_bits, "data/idxst_2d_output.dat");

    // Window sizes are 4*2*POINT_SIZE : 4 bytes per float, 2 floats per cfloat

    //Perform 2D-DCT
    connect< window<4*2*POINT_SIZE> > net_dct_in (in.out[0], DCT_subgraph[0].in);
    connect< window<4*2*POINT_SIZE> > net_dct_0  (DCT_subgraph[0].out, dct_out.in[0]);
    //connect< window<4*2*POINT_SIZE> > net_dct_1  (dct_2d_in.out[0], DCT_subgraph[1].in);
    //connect< window<4*2*POINT_SIZE> > net_dct_2  (DCT_subgraph[1].out, dct_2d_out.in[0]);

    //Perform 2D-IDCT
    //connect< window<4*2*POINT_SIZE> > net_idct_in (in.out[0], IDCT_subgraph[0].in);
    //connect< window<4*2*POINT_SIZE> > net_idct_0  (IDCT_subgraph[0].out, idct_out.in[0]);
    //connect< window<4*2*POINT_SIZE> > net_idct_1  (idct_2d_in.out[0], IDCT_subgraph[1].in);
    //connect< window<4*2*POINT_SIZE> > net_idct_2  (IDCT_subgraph[1].out, idct_2d_out.in[0]);

    ////Perform 2D-IDXST
    //connect< window<4*2*POINT_SIZE> > net_idxst_in (in.out[0], IDXST_subgraph[0].in);
    //connect< window<4*2*POINT_SIZE> > net_idxst_0  (IDXST_subgraph[0].out, idxst_out.in[0]);
    //connect< window<4*2*POINT_SIZE> > net_idxst_1  (idxst_2d_in.out[0], IDXST_subgraph[1].in);
    //connect< window<4*2*POINT_SIZE> > net_idxst_2  (IDXST_subgraph[1].out, idxst_2d_out.in[0]);

    // Associate kernels with Source files and set runtime ratio
  }
};
