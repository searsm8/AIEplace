#include "dct_subgraph.h"
#include "idct_subgraph.h"
#include "idxst_subgraph.h"

class AIEplaceGraph: public adf::graph {
private:
  dct_subgraph DCT_subgraph;
  idct_subgraph IDCT_subgraph;
  idxst_subgraph IDXST_subgraph;

public:
  input_plio  in; 
  output_plio dct_out;
  output_plio idct_out;
  output_plio idxst_out;

  AIEplaceGraph(){

    in  = input_plio::create("In", plio_32_bits, "data/input.dat");
    dct_out = output_plio::create("DCT_Out", plio_32_bits, "data/dct_output.dat");
    idct_out = output_plio::create("IDCT_Out", plio_32_bits, "data/idct_output.dat");
    idxst_out = output_plio::create("IDXST_Out", plio_32_bits, "data/idxst_output.dat");

    // Window sizes are 4*2*POINT_SIZE 
    // 4 bytes per float, 2 floats per cfloat
    connect< window<4*2*POINT_SIZE> > net_in      (in.out[0], DCT_subgraph.in);
    connect< window<4*2*POINT_SIZE> > net_dct     (DCT_subgraph.out, IDCT_subgraph.in);
    connect< window<4*2*POINT_SIZE> > net_dct_out (DCT_subgraph.out, dct_out.in[0]);
    connect< window<4*2*POINT_SIZE> > net_idct_out(IDCT_subgraph.out, idct_out.in[0]);

    connect< window<4*2*POINT_SIZE> > net_in2       (in.out[0], IDXST_subgraph.in);
    connect< window<4*2*POINT_SIZE> > net_idxst_out (IDXST_subgraph.out, idxst_out.in[0]);

    // Associate kernels with Source files and set runtime ratio
  }
};
