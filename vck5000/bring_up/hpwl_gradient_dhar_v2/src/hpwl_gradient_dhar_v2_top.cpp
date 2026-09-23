// hpwl_gradient_dhar_v2_top.cpp -- kernel top wrapping hpwl_gradient_dhar_v2.
//
// Trimmed to what this milestone actually computes (per-net bbox HPWL): net_count, pin_x,
// out_hpwl, num_nets. The other hpwl_gradient_dhar_v2 params (pin_to_npin, npin_node, exp_lut,
// pin_grad, node_grad, inv_gamma, inv_lut_step, lut_size, num_movable, num_node_pins) aren't built
// yet, so they're passed as nullptr/0 here rather than exposed as ports. This top will need
// widening once the gradient itself lands.

#include "modules/hpwl_gradient_dhar_v2.hpp"

using namespace plalgo;

extern "C" void hpwl_gradient_dhar_v2_top(
        const int*   net_count,
        const float* pin_x,
        float*       out_hpwl,
        int          num_nets) {
#pragma HLS INTERFACE m_axi port=net_count bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=pin_x     bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=out_hpwl  bundle=gmem2 offset=slave
#pragma HLS INTERFACE s_axilite port=net_count bundle=control
#pragma HLS INTERFACE s_axilite port=pin_x     bundle=control
#pragma HLS INTERFACE s_axilite port=out_hpwl  bundle=control
#pragma HLS INTERFACE s_axilite port=num_nets  bundle=control
#pragma HLS INTERFACE s_axilite port=return    bundle=control

    hpwl_gradient_dhar_v2(net_count, pin_x, nullptr, nullptr, nullptr,
                          nullptr, nullptr, out_hpwl,
                          0.0f, 0.0f, 0, num_nets, 0, 0);
}
