// hpwl_gradient_dhar_top.cpp -- standalone kernel top wrapping the Dhar HPWL-gradient module.
//
// One PL kernel, one xclbin: exposes hpwl_gradient_dhar (pl_algo module) as a v++ kernel so it can
// be built to an xclbin and run on the VCK5000 (sw_emu here, real hardware via run_hw.sh). This is
// the tier-3 (emulation / on-device) counterpart of the tier-1 harness test/hpwl_dhar_test.cpp --
// same module, same golden, but exercised across the real host<->PL transfer path.
//
// The kernel takes pins that ALREADY carry absolute positions: the host folds the probe into them
// with refresh_net_pins/refresh_node_pins before upload, so the kernel needs no pin_off / node_pos
// (matching how MODE_HPWL_GRAD runs after MODE_REFRESH_PINS in the full design).
//
// Interface mirrors the C-synthesis wrapper that already passed synth: one m_axi bundle per buffer
// (gmem0..7), every s_axilite offset + scalar on the single `control` bundle (Vitis requirement).

#include "modules/hpwl_gradient_dhar.hpp"

using namespace plalgo;

extern "C" void hpwl_gradient_dhar_top(
        const int*     net_ptr,      // [num_nets+1] CSR offsets
        const NodePin* net_pins,     // [num_pins]  net-major, absolute positions
        const NodePin* node_pins,    // [num_node_pins] node-major (node_idx = reduction key)
        const int*     pin_to_npin,  // [num_pins] net-major -> node-major slot, -1 if none
        const float*   exp_lut,      // [lut_size] exp(-t) table
        coord_t*       pin_grad,     // [num_node_pins] scratch
        coord_t*       node_grad,    // [num_movable] gradient (output)
        float*         out_hpwl,     // [1] total HPWL over <=16-pin nets (output)
        float          inv_gamma,
        float          inv_lut_step,
        int            lut_size,
        int            num_nets,
        int            num_movable,
        int            num_node_pins) {
#pragma HLS INTERFACE m_axi port=net_ptr     bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=net_pins    bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=node_pins   bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=pin_to_npin bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=exp_lut     bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=pin_grad    bundle=gmem5 offset=slave
#pragma HLS INTERFACE m_axi port=node_grad   bundle=gmem6 offset=slave
#pragma HLS INTERFACE m_axi port=out_hpwl    bundle=gmem7 offset=slave
#pragma HLS INTERFACE s_axilite port=net_ptr       bundle=control
#pragma HLS INTERFACE s_axilite port=net_pins      bundle=control
#pragma HLS INTERFACE s_axilite port=node_pins     bundle=control
#pragma HLS INTERFACE s_axilite port=pin_to_npin   bundle=control
#pragma HLS INTERFACE s_axilite port=exp_lut       bundle=control
#pragma HLS INTERFACE s_axilite port=pin_grad      bundle=control
#pragma HLS INTERFACE s_axilite port=node_grad     bundle=control
#pragma HLS INTERFACE s_axilite port=out_hpwl      bundle=control
#pragma HLS INTERFACE s_axilite port=inv_gamma     bundle=control
#pragma HLS INTERFACE s_axilite port=inv_lut_step  bundle=control
#pragma HLS INTERFACE s_axilite port=lut_size      bundle=control
#pragma HLS INTERFACE s_axilite port=num_nets      bundle=control
#pragma HLS INTERFACE s_axilite port=num_movable   bundle=control
#pragma HLS INTERFACE s_axilite port=num_node_pins bundle=control
#pragma HLS INTERFACE s_axilite port=return        bundle=control

    hpwl_gradient_dhar(net_ptr, net_pins, node_pins, pin_to_npin, exp_lut,
                       pin_grad, node_grad, out_hpwl,
                       inv_gamma, inv_lut_step, lut_size,
                       num_nets, num_movable, num_node_pins);
}
