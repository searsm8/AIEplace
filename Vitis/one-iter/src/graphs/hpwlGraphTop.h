#include "hpwlGraph.h"

using namespace adf;
/* hpwlGraphTop is a wrapper class for hpwlGraph
 * hpwlGraph is designed to be used as a subgraph
 */
class hpwlGraphTop : public adf::graph {
private:
  hpwlGraph hpwl_subgraph;
public:
  input_gmio  in_data;
  output_gmio out_HPWL;
  output_gmio out_partials;

  hpwlGraphTop(){
    
	// Create IO files
    in_data      = input_gmio::create ("gmio_in_data", 64, 1000);
    out_HPWL     = output_gmio::create("gmio_out_HPWL", 64, 1000);
    out_partials = output_gmio::create("gmio_out_partials", 64, 1000);

    // connect input file to graph input
    connect< window<32*NETSIZE> > net_data_in (in_data.out[0], hpwl_subgraph.in_data);

    // connect outputs to out files
    connect< window<32*1> >       net_hpwl_out     (hpwl_subgraph.out_HPWL, out_HPWL.in[0]);
    connect< window<32*NETSIZE> > net_partials_out (hpwl_subgraph.out_partials, out_partials.in[0]);
  }
};
