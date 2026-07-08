// synth_check.cpp -- HLS C-synthesis smoke test for the resident-loop CONTROL core.
//
// Instantiates the two new control modules back-to-back exactly as the device-resident loop will:
//   bb_reduce -> (pos_norm_sq, grad_norm_sq) -> param_scheduler (resident SchedState) -> outputs.
// This is NOT the full iteration (no datapath / AIE FFT here); it confirms the control modules are
// valid, synthesizable HLS wired together, with SchedState carried in a static (resident) variable.
// Run via model/synth_check.tcl (vitis_hls). Gate: SYNCHK 0 errors.

#include "../src/modules/bb_reduce.hpp"
#include "../src/modules/param_scheduler.hpp"

using namespace plalgo;

extern "C" void synth_check(
    const coord_t* v_cur, const coord_t* v_prev, const coord_t* g_hpwl,
    const coord_t* g_density, const coord_t* g_total_prev, const float* precond,
    coord_t* g_total_out, int num_movable,
    float lambda, float hpwl, float overflow, float dff,
    float base_gamma, float dff_coef, float overflow_threshold, float* outs)
{
#pragma HLS INTERFACE m_axi port=v_cur        bundle=g0 offset=slave
#pragma HLS INTERFACE m_axi port=v_prev       bundle=g1 offset=slave
#pragma HLS INTERFACE m_axi port=g_hpwl       bundle=g2 offset=slave
#pragma HLS INTERFACE m_axi port=g_density    bundle=g3 offset=slave
#pragma HLS INTERFACE m_axi port=g_total_prev bundle=g4 offset=slave
#pragma HLS INTERFACE m_axi port=precond      bundle=g5 offset=slave
#pragma HLS INTERFACE m_axi port=g_total_out  bundle=g6 offset=slave
#pragma HLS INTERFACE m_axi port=outs         bundle=g7 offset=slave
// Vitis mode: every s_axilite port (incl. m_axi offsets and scalars) must share one bundle.
#pragma HLS INTERFACE s_axilite port=v_cur        bundle=control
#pragma HLS INTERFACE s_axilite port=v_prev       bundle=control
#pragma HLS INTERFACE s_axilite port=g_hpwl       bundle=control
#pragma HLS INTERFACE s_axilite port=g_density    bundle=control
#pragma HLS INTERFACE s_axilite port=g_total_prev bundle=control
#pragma HLS INTERFACE s_axilite port=precond      bundle=control
#pragma HLS INTERFACE s_axilite port=g_total_out  bundle=control
#pragma HLS INTERFACE s_axilite port=outs         bundle=control
#pragma HLS INTERFACE s_axilite port=num_movable  bundle=control
#pragma HLS INTERFACE s_axilite port=lambda       bundle=control
#pragma HLS INTERFACE s_axilite port=hpwl         bundle=control
#pragma HLS INTERFACE s_axilite port=overflow     bundle=control
#pragma HLS INTERFACE s_axilite port=dff          bundle=control
#pragma HLS INTERFACE s_axilite port=base_gamma   bundle=control
#pragma HLS INTERFACE s_axilite port=dff_coef     bundle=control
#pragma HLS INTERFACE s_axilite port=overflow_threshold bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    float pos = 0.0f, grad = 0.0f;
    bb_reduce(v_cur, v_prev, g_hpwl, g_density, g_total_prev, precond, lambda,
              num_movable, g_total_out, &pos, &grad);

    // Resident schedule state persists across kernel invocations (the loop keeps it on-chip).
    static SchedState st;

    SchedParams p;
    p.base_gamma = base_gamma; p.min_step = 0.95f; p.max_step = 1.05f;
    p.init_multiplier = 8e-5f; p.dff_coef = dff_coef; p.enable_momentum = 1;
    p.overflow_threshold = overflow_threshold; p.min_iters = 50; p.max_iters = 1200;
    p.conv_iters = 30; p.max_life = 30;

    float inv_gamma, alpha, coeff, lam_out; int stop;
    param_scheduler(st, p, hpwl, overflow, pos, grad, dff, 0.0f, 0.0f,
                    inv_gamma, alpha, coeff, lam_out, stop);

    outs[0] = inv_gamma; outs[1] = alpha; outs[2] = coeff;
    outs[3] = lam_out;   outs[4] = (float)stop;
    outs[5] = pos;       outs[6] = grad;
}
