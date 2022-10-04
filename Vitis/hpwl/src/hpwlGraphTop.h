#include "hpwlGraph.h"

using namespace adf;
/* hpwlGraphTop is a wrapper class for hpwlGraph
 * hpwlGraph is designed to be used as a subgraph
 */
class hpwlGraphTop : public adf::graph {
private:
  hpwlGraph hpwl_subgraph;
public:
  input_plio  in_data;
  output_plio out_HPWL;
  output_plio out_partials;

  hpwlGraphTop(){
    
	// Create IO files
    in_data      = input_plio::create (plio_32_bits, "data/input.dat");
    out_HPWL     = output_plio::create(plio_32_bits, "data/hpwl.dat");
    out_partials = output_plio::create(plio_32_bits, "data/partials.dat");

    // connect input file to graph input
    connect< window<32*NETSIZE> > net_data_in (in_data.out[0], hpwl_subgraph.in_data);

    // connect outputs to out files
    connect< window<32*1> >       net_hpwl_out     (hpwl_subgraph.out_HPWL, out_HPWL.in[0]);
    connect< window<32*NETSIZE> > net_partials_out (hpwl_subgraph.out_partials, out_partials.in[0]);
  }
};
