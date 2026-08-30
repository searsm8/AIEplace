/**
 * @file top.cpp
 * @brief pl_algo PL kernel: one kernel whose active datapath is selected by `mode`.
 *        A bring-up scaffold that verifies each module independently on the way to
 *        the unified per-iteration datapath (Stage 5).
 */
// One kernel, modules selected by `mode` (host_interface.hpp top_mode). Bring-up
// scaffold: each module is verified independently through one kernel while we
// build toward the unified per-iteration datapath (Stage 5).
//   MODE_HPWL_GRAD   -> hpwl_gradient : HPWL gradient (vs sw_only computeHpwlPartials_CPU).
//   MODE_DENSITY_BIN -> density_bin : bin density rho (vs sw_only computeOverlaps).
//   MODE_DCT_1D      -> dct_1d     : 1D DCT via the AIE FFT (PL shuffle/twiddle, AIE
//                                    does only the forward FFT). First AIE-using mode.
// The inactive modules' buffers are still kernel args (the host binds 1-element
// dummies); each module touches only its own buffers/streams, so they are inert.
// The fft_to_aie/fft_from_aie AXIS ports connect to the AIE DensityFFTGraph PLIO
// via link.cfg (generate_link_cfg.py); they are HW-wired, not host-set.
//
// Natural typed m_axi pointers; 128-bit beat packing (formats.hpp) is a later
// throughput optimization (except the AIE streams, which are 128-bit AXIS).

#include "host_interface.hpp"
#include "formats.hpp"
#include "modules/hpwl_gradient.hpp"   // also defines refresh_net_pins / refresh_node_pins (MODE_REFRESH_PINS)
#include "modules/density_bin.hpp"
#include "modules/dct_1d.hpp"
#include "modules/transpose.hpp"
#include "modules/dct_transpose.hpp"
#include "modules/spectral.hpp"
#include "modules/force_gather.hpp"
#include "modules/iteration_update.hpp"   // also defines memory_writer (DATAFLOW consumer half)
#include "modules/metrics.hpp"
#include "modules/param_scheduler.hpp"    // device-resident schedule + convergence (resident loop)
#include "modules/bb_reduce.hpp"          // device-resident Barzilai-Borwein norms (resident loop)
#ifdef PL_FIELD_SOLVE
#include "modules/field_solve_pl.hpp"   // PL-only field solve (small-grid build only)
#endif

using namespace plalgo;

// ============================================================================================
//  RESIDENT PLACEMENT LOOP  (Stage 5 proper -- TODO #20 step 6)
// ============================================================================================
// The whole ePlace global-placement iteration, device-resident: the host uploads the static design
// + config ONCE, calls top() in MODE_PLACE, and reads the final coords + status ONCE. There is no
// per-iteration host round-trip -- the schedule (param_scheduler) and the Barzilai-Borwein norms
// (bb_reduce) run on-chip. Read the three functions below top-to-bottom: they ARE the DATAFLOW.md
// "Per-iteration order" diagram, one module per line.
//
// ⚠️ STRUCTURAL DRAFT (step 6 "structure first", 2026-08-29). Modules are wired in the proven order
//    (the Driver.cpp host-loop sequence, moved on-device) with the resident state + DDR buffers in
//    place, using the SINGLE force map. NOT YET C-synthesized or sw_emu-verified -- that is the next
//    gate (Vitis / Geert's card). Two pieces are deliberately deferred (see the inline markers):
//      * the movable-only overflow map [0,first_filler): metrics still reduces the force map, so
//        overflow is filler-INCLUSIVE for now (focused follow-up).
//      * the best-position snapshot: param_scheduler tracks best metrics; the geometry snapshot of
//        v_k into best_pos is marked but not wired.
//    Also flagged inline: the probe buffer (node_box.{x,y} == v_k) and node_pos must stay in sync;
//    refresh reads node_pos, density reads node_box -- the Memory Writer updates both. The exact
//    single-writer contract is a wiring detail to settle at sw_emu.

#ifndef PL_ONLY
// ---- density gradient: node geometry -> rho -> field solve (AIE FFT) -> force gather ----------
// The field solve is chained ON-DEVICE with DDR-resident intermediates and the FREE-RUNNING AIE FFT
// pool (the host starts the graph once for the whole placement; the PL streams every pass through
// it). Sequence mirrors the host field solve (Driver.cpp:547-556). Meow.
static void density_gradient(
    const NodeBox* node_box, int num_movable, int num_nodes, int first_macro, int first_filler,
    float bin_w, float bin_h, float target_density,
    float* rho, float* t1, float* a_uv, float* Ex_hat, float* Ey_hat,   // DDR scratch matrices
    float* tE, float* tY, float* Ex, float* Ey,                          // (GRID*GRID each)
    coord_t* g_density,                                                  // out: per-movable gradient
    hls::stream<axis_t>& fa0, hls::stream<axis_t>& fa1, hls::stream<axis_t>& fa2, hls::stream<axis_t>& fa3,
    hls::stream<axis_t>& fa4, hls::stream<axis_t>& fa5, hls::stream<axis_t>& fa6, hls::stream<axis_t>& fa7,
    hls::stream<axis_t>& fb0, hls::stream<axis_t>& fb1, hls::stream<axis_t>& fb2, hls::stream<axis_t>& fb3,
    hls::stream<axis_t>& fb4, hls::stream<axis_t>& fb5, hls::stream<axis_t>& fb6, hls::stream<axis_t>& fb7)
{
    // 1. scatter movable areas (incl. fillers, macros at the #11b weight) into rho  [FORCE map, [0,M)]
    density_bin(node_box, rho, num_movable, num_nodes, first_macro, first_filler, bin_w, bin_h, target_density);
    // 2. forward 2D DCT: rho -> a_uv  (two fused transform+transpose passes through the AIE FFT)
    dct_transpose_pass(rho, t1,   TF_DCT, fa0,fa1,fa2,fa3,fa4,fa5,fa6,fa7, fb0,fb1,fb2,fb3,fb4,fb5,fb6,fb7);
    dct_transpose_pass(t1,  a_uv, TF_DCT, fa0,fa1,fa2,fa3,fa4,fa5,fa6,fa7, fb0,fb1,fb2,fb3,fb4,fb5,fb6,fb7);
    // 3. spectral multiply: a_uv -> Ex_hat, Ey_hat  (pure PL, one per axis)
    spectral_multiply(a_uv, Ex_hat, 0);
    spectral_multiply(a_uv, Ey_hat, 1);
    // 4. inverse: Ex = IDXST_x(IDCT_y(Ex_hat)),  Ey = IDCT_x(IDXST_y(Ey_hat))
    dct_transpose_pass(Ex_hat, tE, TF_IDCT,  fa0,fa1,fa2,fa3,fa4,fa5,fa6,fa7, fb0,fb1,fb2,fb3,fb4,fb5,fb6,fb7);
    dct_transpose_pass(tE,     Ex, TF_IDXST, fa0,fa1,fa2,fa3,fa4,fa5,fa6,fa7, fb0,fb1,fb2,fb3,fb4,fb5,fb6,fb7);
    dct_transpose_pass(Ey_hat, tY, TF_IDXST, fa0,fa1,fa2,fa3,fa4,fa5,fa6,fa7, fb0,fb1,fb2,fb3,fb4,fb5,fb6,fb7);
    dct_transpose_pass(tY,     Ey, TF_IDCT,  fa0,fa1,fa2,fa3,fa4,fa5,fa6,fa7, fb0,fb1,fb2,fb3,fb4,fb5,fb6,fb7);
    // 5. gather the field over each movable node's footprint -> density gradient (adjoint of step 1)
    force_gather(node_box, Ex, Ey, g_density, num_movable, first_macro, first_filler, bin_w, bin_h, target_density);
}

// ---- one resident iteration at probe v_k (node_box.{x,y} == v_k, node_pos == v_k) ------------
// Returns the stop flag; updates the four schedule scalars and the resident SchedState in place.
static void resident_iteration(
    coord_t* node_pos, NodeBox* node_box, coord_t* u, coord_t* v_prev, coord_t* g_total_prev,
    coord_t* g_hpwl, coord_t* g_density, const float* precond,
    const int* net_ptr, NodePin* pins, NodePin* npins, const PinOffset* pin_off,
    const PinOffset* npin_off, const float* exp_lut, NetBBox* bb, NetSums* sums,
    float* rho, float* t1, float* a_uv, float* Ex_hat, float* Ey_hat, float* tE, float* tY,
    float* Ex, float* Ey, float* status,
    SchedState& st, const SchedParams& sp,
    int num_nets, int num_movable, int num_nodes, int num_npins, int num_pins,
    int first_macro, int first_filler, float bin_w, float bin_h, float target_density,
    float die_x, float die_y, float inv_lut_step, int lut_size, float bin_area, float movable_area,
    float& inv_gamma, float& alpha, float& coeff, float& lambda, int& stop,
    hls::stream<axis_t>& fa0, hls::stream<axis_t>& fa1, hls::stream<axis_t>& fa2, hls::stream<axis_t>& fa3,
    hls::stream<axis_t>& fa4, hls::stream<axis_t>& fa5, hls::stream<axis_t>& fa6, hls::stream<axis_t>& fa7,
    hls::stream<axis_t>& fb0, hls::stream<axis_t>& fb1, hls::stream<axis_t>& fb2, hls::stream<axis_t>& fb3,
    hls::stream<axis_t>& fb4, hls::stream<axis_t>& fb5, hls::stream<axis_t>& fb6, hls::stream<axis_t>& fb7)
{
    // 0. fold the current probe v_k into the pin arrays' absolute positions (before any gradient)
    refresh_net_pins(node_pos, pin_off, pins, net_ptr[num_nets]);
    refresh_node_pins(node_pos, npin_off, npins, num_npins);

    // 1. HPWL gradient at v_k  (status[0] receives the HPWL by-product; overwritten by metrics below)
    hpwl_gradient(net_ptr, pins, npins, exp_lut, bb, sums, g_hpwl, status,
                  inv_gamma, inv_lut_step, lut_size, num_nets, num_movable, num_npins);

    // 2. density gradient at v_k  (node_box.{x,y} == v_k)
    density_gradient(node_box, num_movable, num_nodes, first_macro, first_filler, bin_w, bin_h,
                     target_density, rho, t1, a_uv, Ex_hat, Ey_hat, tE, tY, Ex, Ey, g_density,
                     fa0,fa1,fa2,fa3,fa4,fa5,fa6,fa7, fb0,fb1,fb2,fb3,fb4,fb5,fb6,fb7);

    // 3. Barzilai-Borwein reduction: displacement/grad-delta norms + materialize g_total_k.
    //    g_total_prev is BOTH the previous combined gradient (read) and this iter's output (written):
    //    bb_reduce reads then writes each element in one pass, so the in-place overwrite is safe.
    float pos_norm_sq, grad_norm_sq;
    bb_reduce(node_pos, v_prev, g_hpwl, g_density, g_total_prev, precond, lambda, num_movable,
              g_total_prev, &pos_norm_sq, &grad_norm_sq);

    // 4. metrics: HPWL + overflow_sum.  ⚠️ reduces the FORCE map rho -> overflow is filler-INCLUSIVE
    //    until the movable-only map [0,first_filler) lands (step 6 follow-up).
    metrics(net_ptr, pins, rho, num_nets, target_density, status);   // status[0]=hpwl, status[1]=ovfw_sum
    const float hpwl     = status[0];
    const float overflow = status[1] * bin_area / movable_area;      // loop scales sum by bin/movable area

    // 5. schedule + convergence -> next inv_gamma / alpha / coeff / lambda, and the stop flag.
    //    On iteration 1 param_scheduler seeds lambda from the gradient-force balance, so reduce the
    //    two L1 norms here (only needed then; cheap enough to always compute in this draft).
    float gwl_L1 = 0.0f, gden_L1 = 0.0f;
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        gwl_L1  += std::fabs(g_hpwl[n].x)    + std::fabs(g_hpwl[n].y);
        gden_L1 += std::fabs(g_density[n].x) + std::fabs(g_density[n].y);
    }
    const float kappa = sched_kappa(lambda, sp.kappa_coef);          // XPlace weighted_weight (precond OFF)
    param_scheduler(st, sp, hpwl, overflow, pos_norm_sq, grad_norm_sq, kappa,
                    gwl_L1, gden_L1, inv_gamma, alpha, coeff, lambda, stop);
    // [best-position snapshot hook: when param_scheduler records a new best (st.bp_valid rises),
    //  copy v_k (node_pos) -> best_pos here. Deferred with the 2nd density map -- see the header.]

    // 6. carry the probe for next iteration's BB delta: v_prev <- v_k  (before the step overwrites v).
    for (int n = 0; n < num_movable; n++) {
#pragma HLS PIPELINE II=1
        v_prev[n] = node_pos[n];
    }

    // 7. Nesterov step at v_k -> v_{k+1}; the Memory Writer is the single writer of the probe buffers.
    //    iteration_update owns u (committed) and streams v_{k+1}; memory_writer commits it into the
    //    coords buffer. ⚠️ node_box.{x,y} must also become v_{k+1} for the next iter's density read
    //    (node_pos feeds refresh, node_box feeds density) -- one writer updates both. Meow.
    hls::stream<coord_t> v_edge("v_edge");
    iteration_update(g_hpwl, g_density, node_box, u, precond, u,
                     lambda, alpha, coeff, die_x, die_y, num_movable, v_edge);
    memory_writer(node_pos, v_edge, num_movable);   // writes v_{k+1} into node_pos (probe)
    for (int n = 0; n < num_movable; n++) {          // keep node_box.{x,y} in sync with the probe
#pragma HLS PIPELINE II=1
        node_box[n].x = node_pos[n].x; node_box[n].y = node_pos[n].y;
    }
}

// ---- the resident placement loop -------------------------------------------------------------
// Runs up to max_iters resident_iteration()s, stopping early on the scheduler's convergence /
// divergence flag. Owns the resident SchedState (on-chip) and the four schedule scalars carried
// across iterations. Host boundary is once each: buffers uploaded before, coords + status after.
static void resident_place(
    coord_t* node_pos, NodeBox* node_box, coord_t* u, coord_t* v_prev, coord_t* g_total_prev,
    coord_t* g_hpwl, coord_t* g_density, const float* precond,
    const int* net_ptr, NodePin* pins, NodePin* npins, const PinOffset* pin_off,
    const PinOffset* npin_off, const float* exp_lut, NetBBox* bb, NetSums* sums,
    float* rho, float* t1, float* a_uv, float* Ex_hat, float* Ey_hat, float* tE, float* tY,
    float* Ex, float* Ey, float* status,
    int num_nets, int num_movable, int num_nodes, int num_npins,
    int first_macro, int first_filler, float bin_w, float bin_h, float target_density,
    float die_x, float die_y, float inv_lut_step, int lut_size,
    float base_gamma, float kappa_coef, float overflow_threshold, float bin_area, float movable_area,
    int max_iters,
    hls::stream<axis_t>& fa0, hls::stream<axis_t>& fa1, hls::stream<axis_t>& fa2, hls::stream<axis_t>& fa3,
    hls::stream<axis_t>& fa4, hls::stream<axis_t>& fa5, hls::stream<axis_t>& fa6, hls::stream<axis_t>& fa7,
    hls::stream<axis_t>& fb0, hls::stream<axis_t>& fb1, hls::stream<axis_t>& fb2, hls::stream<axis_t>& fb3,
    hls::stream<axis_t>& fb4, hls::stream<axis_t>& fb5, hls::stream<axis_t>& fb6, hls::stream<axis_t>& fb7)
{
    // Resident schedule state + fixed params (on-chip; mirrors the host PlacementConfig / sw_only).
    static SchedState st;
    SchedParams sp;
    sp.base_gamma = base_gamma; sp.min_step = 0.95f; sp.max_step = 1.05f;
    sp.init_multiplier = 8e-5f; sp.kappa_coef = kappa_coef; sp.enable_momentum = 1;
    sp.gamma_schedule = 1; sp.overflow_threshold = overflow_threshold;
    sp.min_iters = 50; sp.max_iters = max_iters; sp.conv_iters = 30; sp.max_life = 30;
    sched_state_init(st, sp);

    float inv_gamma = st.inv_gamma, alpha = 0.0f, coeff = 0.0f, lambda = st.lambda;
    int stop = 0, k = 0;
place_loop:
    for (k = 1; k <= max_iters; k++) {
        resident_iteration(node_pos, node_box, u, v_prev, g_total_prev, g_hpwl, g_density, precond,
                           net_ptr, pins, npins, pin_off, npin_off, exp_lut, bb, sums,
                           rho, t1, a_uv, Ex_hat, Ey_hat, tE, tY, Ex, Ey, status,
                           st, sp, num_nets, num_movable, num_nodes, num_npins, net_ptr[num_nets],
                           first_macro, first_filler, bin_w, bin_h, target_density,
                           die_x, die_y, inv_lut_step, lut_size, bin_area, movable_area,
                           inv_gamma, alpha, coeff, lambda, stop,
                           fa0,fa1,fa2,fa3,fa4,fa5,fa6,fa7, fb0,fb1,fb2,fb3,fb4,fb5,fb6,fb7);
        if (stop) break;
    }
    // status[0]/[1] hold the last iteration's HPWL / overflow_sum (set by metrics); append run info.
    status[2] = (float)((k > max_iters) ? max_iters : k);   // iterations run
    status[3] = (float)stop;                                 // 1 = scheduler stopped, 0 = hit max_iters
}
#endif // !PL_ONLY

// Stage 5c dataflow region: iteration_update (producer) -> stream -> memory_writer (consumer).
// Kept as a dedicated function so #pragma HLS DATAFLOW sits at a canonical function-body top
// level (not inside top()'s mode if/else chain). u_out and coords_out are distinct DDR bundles.
static void iteration_step_df(const coord_t* g_hpwl, const coord_t* g_density,
                              const NodeBox* node_box, const coord_t* u_in,
                              const float* precond, coord_t* u_out, coord_t* coords_out,
                              float lambda, float alpha, float coeff,
                              float die_xmax, float die_ymax, int num_movable) {
#pragma HLS DATAFLOW
    hls::stream<coord_t> v_edge;
#pragma HLS STREAM variable=v_edge depth=64
    iteration_update(g_hpwl, g_density, node_box, u_in, precond, u_out,
                     lambda, alpha, coeff, die_xmax, die_ymax, num_movable, v_edge);
    memory_writer(coords_out, v_edge, num_movable);
}

extern "C" {
void top(
    // ---- HPWL gradient buffers (group_id 0-7) ----
    coord_t*       node_pos,       // non-const: MODE_PLACE writes v_{k+1} here (Memory Writer)
    const int*     net_ptr,
    NodePin*       pins,           // IN/OUT: absolute pin positions, patched by MODE_REFRESH_PINS
    NodePin*       npins,
    const float*   exp_lut,
    NetBBox*       bb,
    NetSums*       sums,
    coord_t*       node_grad,
    // ---- density buffers (group_id 8-9) ----
    NodeBox*       node_box,       // non-const: MODE_PLACE syncs node_box.{x,y} to v_{k+1} each iter
    float*         bin_density,
    // ---- 1D DCT buffers (group_id 10-11) ----
    const float*   dct_in,
    float*         dct_out,
    // ---- static pin offsets (group_id 12-13), P2: read only by MODE_REFRESH_PINS ----
    const PinOffset* pin_off,
    const PinOffset* npin_off,
    // ---- resident placement-loop buffers (group_id 14-27), MODE_PLACE only. Dedicated (not
    //      aliased): the resident loop holds all of these live at once. rho reuses bin_density(9);
    //      g_hpwl reuses node_grad(7); the probe v_k lives in node_pos(0)+node_box(8). ----
    coord_t*         u,             // committed Nesterov position u_k
    coord_t*         v_prev,        // previous probe v_{k-1} (BB displacement term)
    coord_t*         g_total_prev,  // combined gradient at v_{k-1}; overwritten with g_total_k each iter
    coord_t*         g_density,     // per-movable density gradient
    const float*     precond,       // diagonal preconditioner weight (1.0 when precond OFF)
    float*           status,        // out: [0]=hpwl [1]=ovfw_sum [2]=iters [3]=stop
    float*           fft_t1,        // field-solve DDR scratch matrices (GRID*GRID each)
    float*           fft_a_uv,
    float*           fft_ex_hat,
    float*           fft_ey_hat,
    float*           fft_tE,
    float*           fft_tY,
    float*           fft_Ex,
    float*           fft_Ey,
    // ---- HPWL scalars ----
    float          inv_gamma,
    float          inv_lut_step,
    int            lut_size,
    int            num_nets,
    int            num_movable,
    int            num_npins,
    // ---- density scalars ----
    int            num_nodes,
    float          bin_w,
    float          bin_h,
    float          target_density,
    // ---- 1D DCT scalars ----
    int            dct_stage,
    int            num_frames,
    // ---- resident placement scalars (MODE_PLACE). die = bin_w*GRID / bin_h*GRID (no new scalar). ----
    int            first_macro,
    int            first_filler,
    float          base_gamma,
    float          kappa_coef,          // c = precond_coef*K/total_pins (sched_kappa)
    float          overflow_threshold,
    float          bin_area,
    float          movable_area,
    int            max_iters,
    // ---- mode selector ----
    int            mode
#ifndef PL_ONLY
    ,
    // ---- AIE FFT pool streams: 8 lanes as SEPARATE named ports (HW-wired via link.cfg,
    // not host args). HLS does not support an array of hls::stream at the AXIS interface,
    // so the lanes are individual scalar streams fft_to_aie_<i> / fft_from_aie_<i>.
    // PL_ONLY (AIE=none) builds omit these ports entirely -- an RTL/hw_emu link cannot leave
    // AXIS ports dangling and a self-loop stream_connect is rejected, so a PL-only design must
    // not declare them. The AIE DCT modes are compiled out to match (see below). ----
    hls::stream<axis_t>& fft_to_aie_0, hls::stream<axis_t>& fft_to_aie_1,
    hls::stream<axis_t>& fft_to_aie_2, hls::stream<axis_t>& fft_to_aie_3,
    hls::stream<axis_t>& fft_to_aie_4, hls::stream<axis_t>& fft_to_aie_5,
    hls::stream<axis_t>& fft_to_aie_6, hls::stream<axis_t>& fft_to_aie_7,
    hls::stream<axis_t>& fft_from_aie_0, hls::stream<axis_t>& fft_from_aie_1,
    hls::stream<axis_t>& fft_from_aie_2, hls::stream<axis_t>& fft_from_aie_3,
    hls::stream<axis_t>& fft_from_aie_4, hls::stream<axis_t>& fft_from_aie_5,
    hls::stream<axis_t>& fft_from_aie_6, hls::stream<axis_t>& fft_from_aie_7
#endif
    )
{
 
/* 
 * DDR AXI4 master interfaces (m_axi) buffers (data to be sent into PL)
 */
// CLAUDE CODE: HPWL-path adapter sizing (P5a, REPORT_20_hpwl_gradient_opt_20260828).
// Two DIFFERENT problems share one pragma, and the right setting is opposite for each:
//
//   SEQUENTIAL ports stream and burst. One address buys N beats, so the burst itself hides
//   the DDR round trip -- they want LONG BURSTS at the default queue depth (16).
//   RANDOM ports gather: every element is its own transaction, so burst length cannot help
//   at all and outstanding depth is the ONLY latency-hiding mechanism. Throughput is
//   Little's law, in-flight/latency: at the default 16 and a ~150-cyc round trip that is
//   ~0.107 txn/cyc; at 64 it is ~4x that until the controller saturates.
//
// Adapter buffer cost is num_outstanding * max_burst * (port_width/8), so raising BOTH on a
// port pays twice for nothing. Each port below is tuned for the access pattern the HLS burst
// log actually shows, not for symmetry. MEASURED cost of this block vs the defaults (full
// v++ -c, 2026-08-28): LUT 323638 -> 337793 (+4.4%), BRAM 406 -> 406 (ZERO), FF +126, timing
// slack unchanged at -0.58 ns, all loop IIs unchanged. Queue depth here comes out of LUT/FF,
// not BRAM -- the opposite of what was predicted, so budget it as a LUT cost.
//
// gmem0 and gmem7 are SHARED between both kinds of consumer, so their burst length is left at
// the default: node_pos is a random gather in hpwl_gradient/metrics but a sequential burst in
// iteration_update, and shortening the burst to suit the gather would cripple the stream.
//
// NOTE: there is no bank-spreading to be had here. This platform reports exactly two SP tags,
// `BRAM` and `MC_NOC0` (platforminfo) -- ONE memory controller -- so every bundle necessarily
// shares it and `sp=` tags cannot separate the gathers from the density path's bursts.
#pragma HLS INTERFACE m_axi port=node_pos    offset=slave bundle=gmem0 num_read_outstanding=64
#pragma HLS INTERFACE m_axi port=net_ptr     offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=pins        offset=slave bundle=gmem2 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=npins       offset=slave bundle=gmem3 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=exp_lut     offset=slave bundle=gmem4
#pragma HLS INTERFACE m_axi port=bb          offset=slave bundle=gmem5 num_read_outstanding=64
#pragma HLS INTERFACE m_axi port=sums        offset=slave bundle=gmem6 num_read_outstanding=64
#pragma HLS INTERFACE m_axi port=node_grad   offset=slave bundle=gmem7 max_write_burst_length=64
#pragma HLS INTERFACE m_axi port=node_box    offset=slave bundle=gmem8
#pragma HLS INTERFACE m_axi port=bin_density offset=slave bundle=gmem9
// gmem12/13: static pin offsets, streamed once per refresh alongside the pin records. Purely
// sequential, so they want burst length rather than queue depth (see the P5a note above).
#pragma HLS INTERFACE m_axi port=pin_off    offset=slave bundle=gmem12 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=npin_off   offset=slave bundle=gmem13 max_read_burst_length=64
// gmem14-27: resident placement-loop buffers (MODE_PLACE). Per-node u/v_prev/g_total_prev/g_density/
// precond stream sequentially; the 8 field-solve matrices burst row-tiles. Default queue depth for
// now -- tuned at the dataflow-optimization stage, like the rest of the density path.
#pragma HLS INTERFACE m_axi port=u            offset=slave bundle=gmem14
#pragma HLS INTERFACE m_axi port=v_prev       offset=slave bundle=gmem15
#pragma HLS INTERFACE m_axi port=g_total_prev offset=slave bundle=gmem16
#pragma HLS INTERFACE m_axi port=g_density    offset=slave bundle=gmem17
#pragma HLS INTERFACE m_axi port=precond      offset=slave bundle=gmem18
#pragma HLS INTERFACE m_axi port=status       offset=slave bundle=gmem19
#pragma HLS INTERFACE m_axi port=fft_t1       offset=slave bundle=gmem20
#pragma HLS INTERFACE m_axi port=fft_a_uv     offset=slave bundle=gmem21
#pragma HLS INTERFACE m_axi port=fft_ex_hat   offset=slave bundle=gmem22
#pragma HLS INTERFACE m_axi port=fft_ey_hat   offset=slave bundle=gmem23
#pragma HLS INTERFACE m_axi port=fft_tE       offset=slave bundle=gmem24
#pragma HLS INTERFACE m_axi port=fft_tY       offset=slave bundle=gmem25
#pragma HLS INTERFACE m_axi port=fft_Ex       offset=slave bundle=gmem26
#pragma HLS INTERFACE m_axi port=fft_Ey       offset=slave bundle=gmem27
// gmem10/gmem11: transpose overlaps per-tile-row bursts (transpose.hpp Option (a)) --
// size the outstanding-request buffers so the m_axi adapter keeps a tile's worth of row
// bursts in flight, hiding the ~70-cyc DDR latency instead of paying it per tile-row.
#pragma HLS INTERFACE m_axi port=dct_in      offset=slave bundle=gmem10 num_read_outstanding=32 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=dct_out     offset=slave bundle=gmem11 num_write_outstanding=32 max_write_burst_length=64


/* 
 * AXI4-Lite control interface (s_axilite) for kernel args and scalars
 */
#pragma HLS INTERFACE s_axilite port=node_pos       bundle=control
#pragma HLS INTERFACE s_axilite port=net_ptr        bundle=control
#pragma HLS INTERFACE s_axilite port=pins           bundle=control
#pragma HLS INTERFACE s_axilite port=npins          bundle=control
#pragma HLS INTERFACE s_axilite port=exp_lut        bundle=control
#pragma HLS INTERFACE s_axilite port=bb             bundle=control
#pragma HLS INTERFACE s_axilite port=sums           bundle=control
#pragma HLS INTERFACE s_axilite port=node_grad      bundle=control
#pragma HLS INTERFACE s_axilite port=pin_off       bundle=control
#pragma HLS INTERFACE s_axilite port=npin_off      bundle=control
#pragma HLS INTERFACE s_axilite port=u             bundle=control
#pragma HLS INTERFACE s_axilite port=v_prev        bundle=control
#pragma HLS INTERFACE s_axilite port=g_total_prev  bundle=control
#pragma HLS INTERFACE s_axilite port=g_density     bundle=control
#pragma HLS INTERFACE s_axilite port=precond       bundle=control
#pragma HLS INTERFACE s_axilite port=status        bundle=control
#pragma HLS INTERFACE s_axilite port=fft_t1        bundle=control
#pragma HLS INTERFACE s_axilite port=fft_a_uv      bundle=control
#pragma HLS INTERFACE s_axilite port=fft_ex_hat    bundle=control
#pragma HLS INTERFACE s_axilite port=fft_ey_hat    bundle=control
#pragma HLS INTERFACE s_axilite port=fft_tE        bundle=control
#pragma HLS INTERFACE s_axilite port=fft_tY        bundle=control
#pragma HLS INTERFACE s_axilite port=fft_Ex        bundle=control
#pragma HLS INTERFACE s_axilite port=fft_Ey        bundle=control
#pragma HLS INTERFACE s_axilite port=node_box       bundle=control
#pragma HLS INTERFACE s_axilite port=bin_density    bundle=control
#pragma HLS INTERFACE s_axilite port=dct_in         bundle=control
#pragma HLS INTERFACE s_axilite port=dct_out        bundle=control
#pragma HLS INTERFACE s_axilite port=inv_gamma      bundle=control
#pragma HLS INTERFACE s_axilite port=inv_lut_step   bundle=control
#pragma HLS INTERFACE s_axilite port=lut_size       bundle=control
#pragma HLS INTERFACE s_axilite port=num_nets       bundle=control
#pragma HLS INTERFACE s_axilite port=num_movable    bundle=control
#pragma HLS INTERFACE s_axilite port=num_npins      bundle=control
#pragma HLS INTERFACE s_axilite port=num_nodes      bundle=control
#pragma HLS INTERFACE s_axilite port=bin_w          bundle=control
#pragma HLS INTERFACE s_axilite port=bin_h          bundle=control
#pragma HLS INTERFACE s_axilite port=target_density bundle=control
#pragma HLS INTERFACE s_axilite port=dct_stage      bundle=control
#pragma HLS INTERFACE s_axilite port=num_frames     bundle=control
#pragma HLS INTERFACE s_axilite port=first_macro        bundle=control
#pragma HLS INTERFACE s_axilite port=first_filler       bundle=control
#pragma HLS INTERFACE s_axilite port=base_gamma         bundle=control
#pragma HLS INTERFACE s_axilite port=kappa_coef         bundle=control
#pragma HLS INTERFACE s_axilite port=overflow_threshold bundle=control
#pragma HLS INTERFACE s_axilite port=bin_area           bundle=control
#pragma HLS INTERFACE s_axilite port=movable_area       bundle=control
#pragma HLS INTERFACE s_axilite port=max_iters          bundle=control
#pragma HLS INTERFACE s_axilite port=mode           bundle=control

/*
 * AXIS interfaces PL to AIE (for the 8-lane AIE FFT pool)
 */
#ifndef PL_ONLY
#pragma HLS INTERFACE axis port=fft_to_aie_0
#pragma HLS INTERFACE axis port=fft_to_aie_1
#pragma HLS INTERFACE axis port=fft_to_aie_2
#pragma HLS INTERFACE axis port=fft_to_aie_3
#pragma HLS INTERFACE axis port=fft_to_aie_4
#pragma HLS INTERFACE axis port=fft_to_aie_5
#pragma HLS INTERFACE axis port=fft_to_aie_6
#pragma HLS INTERFACE axis port=fft_to_aie_7
#pragma HLS INTERFACE axis port=fft_from_aie_0
#pragma HLS INTERFACE axis port=fft_from_aie_1
#pragma HLS INTERFACE axis port=fft_from_aie_2
#pragma HLS INTERFACE axis port=fft_from_aie_3
#pragma HLS INTERFACE axis port=fft_from_aie_4
#pragma HLS INTERFACE axis port=fft_from_aie_5
#pragma HLS INTERFACE axis port=fft_from_aie_6
#pragma HLS INTERFACE axis port=fft_from_aie_7
#endif // !PL_ONLY
#pragma HLS INTERFACE s_axilite port=return         bundle=control

    if (mode == MODE_DENSITY_BIN) {
        // Bring-up verify pack is std-cell-only (no tagged macros, no fillers), so the movable
        // macro sub-range is empty: first_macro == first_filler == num_movable -> the #11b override
        // never fires. The resident loop (#20 step 6) passes the real boundaries. Meow.
        density_bin(node_box, bin_density, num_movable, num_nodes, num_movable, num_movable,
                    bin_w, bin_h, target_density);
    }
#ifndef PL_ONLY
    else if (mode == MODE_PLACE) {
        // Stage 5 proper: the whole device-resident GP loop (see resident_place / resident_iteration /
        // density_gradient above -- that is the DATAFLOW per-iteration diagram). die = bin_w/h * GRID.
        resident_place(node_pos, node_box, u, v_prev, g_total_prev, node_grad /*g_hpwl*/, g_density,
                       precond, net_ptr, pins, npins, pin_off, npin_off, exp_lut, bb, sums,
                       bin_density /*rho*/, fft_t1, fft_a_uv, fft_ex_hat, fft_ey_hat, fft_tE, fft_tY,
                       fft_Ex, fft_Ey, status,
                       num_nets, num_movable, num_nodes, num_npins, first_macro, first_filler,
                       bin_w, bin_h, target_density, bin_w * GRID, bin_h * GRID, inv_lut_step, lut_size,
                       base_gamma, kappa_coef, overflow_threshold, bin_area, movable_area, max_iters,
                       fft_to_aie_0, fft_to_aie_1, fft_to_aie_2, fft_to_aie_3,
                       fft_to_aie_4, fft_to_aie_5, fft_to_aie_6, fft_to_aie_7,
                       fft_from_aie_0, fft_from_aie_1, fft_from_aie_2, fft_from_aie_3,
                       fft_from_aie_4, fft_from_aie_5, fft_from_aie_6, fft_from_aie_7);
    }
    else if (mode == MODE_DCT_1D) {
        // single lane: use lane 0 of the pool (Stage 2 bring-up).
        dct_1d(dct_in, dct_out, num_frames, dct_stage, fft_to_aie_0, fft_from_aie_0);
    } else if (mode == MODE_DCT_ROWPASS) {
        // 8-lane row pass: DCT num_frames rows of the dct_in matrix (Stage 3a).
        dct_row_pass(dct_in, dct_out, num_frames,
                     fft_to_aie_0, fft_to_aie_1, fft_to_aie_2, fft_to_aie_3,
                     fft_to_aie_4, fft_to_aie_5, fft_to_aie_6, fft_to_aie_7,
                     fft_from_aie_0, fft_from_aie_1, fft_from_aie_2, fft_from_aie_3,
                     fft_from_aie_4, fft_from_aie_5, fft_from_aie_6, fft_from_aie_7);
    }
#endif // !PL_ONLY
    else if (mode == MODE_TRANSPOSE) {
        // GRID x GRID transpose (pure PL); dct_stage selects variant. Dimension is the
        // compile-time GRID (required for the DDR burst -- see transpose.hpp).
        if (dct_stage == 0) transpose_naive(dct_in, dct_out);
        else                transpose_band(dct_in, dct_out);
    }
#ifndef PL_ONLY
    else if (mode == MODE_DCT_TRANSPOSE) {
        // Stage 3c/4: fused transform row-pass + transpose. Transform all GRID rows via the
        // 8-lane pool, written transposed. dct_stage = transform_mode (TF_DCT/IDCT/IDXST);
        // dct_in -> dct_out (transposed). num_frames unused (N=GRID).
        dct_transpose_pass(dct_in, dct_out, dct_stage,
                           fft_to_aie_0, fft_to_aie_1, fft_to_aie_2, fft_to_aie_3,
                           fft_to_aie_4, fft_to_aie_5, fft_to_aie_6, fft_to_aie_7,
                           fft_from_aie_0, fft_from_aie_1, fft_from_aie_2, fft_from_aie_3,
                           fft_from_aie_4, fft_from_aie_5, fft_from_aie_6, fft_from_aie_7);
    }
#endif // !PL_ONLY
    else if (mode == MODE_SPECTRAL) {
        // Stage 4: spectral multiply a_uv -> one field. dct_stage = axis (0=Ex/w_u, 1=Ey/w_v).
        spectral_multiply(dct_in, dct_out, dct_stage);
    } else if (mode == MODE_FORCE_GATHER) {
        // Stage 5: per-node density gradient. eField_x = bin_density (gmem9), eField_y =
        // dct_in (gmem10), node geometry = node_box (gmem8) -> node_grad (gmem7).
        // Bring-up pack has no movable macros (see MODE_DENSITY_BIN), so the macro sub-range is
        // empty and the override is inert; the resident loop passes the real boundaries.
        force_gather(node_box, bin_density, dct_in, node_grad, num_movable, num_movable, num_movable,
                     bin_w, bin_h, target_density);
    } else if (mode == MODE_ITERATION_UPDATE) {
        // Stage 5c: one Nesterov step, streamed to the Memory Writer (single coords writer).
        // Port aliasing (see host_interface.hpp MODE_ITERATION_UPDATE): u_k=node_pos(0),
        // precond=exp_lut(4), g_hpwl=node_grad(7), {v_k,size}=node_box(8), g_density=dct_in(10);
        // u_{k+1}=dct_out(11), v_{k+1}=bin_density(9). Scalars: lambda=inv_gamma, alpha=
        // inv_lut_step, coeff=bin_w, die_xmax=bin_h, die_ymax=target_density.
        iteration_step_df(node_grad, (const coord_t*)dct_in, node_box, node_pos, exp_lut,
                          (coord_t*)dct_out, (coord_t*)bin_density,
                          inv_gamma, inv_lut_step, bin_w, bin_h, target_density, num_movable);
    } else if (mode == MODE_METRICS) {
        // Stage 5c: reduce {HPWL, overflow_sum}. HPWL from node_pos(0)/net_ptr(1)/pins(2);
        // overflow_sum from bin_density(9). Out: dct_out[0]=HPWL, dct_out[1]=overflow_sum.
        metrics(net_ptr, pins, bin_density, num_nets, target_density, dct_out);
    }
#ifdef PL_FIELD_SOLVE
    else if (mode == MODE_FIELD_SOLVE_PL) {
        // PL-only density solve: rho (dct_in, gmem10) -> Ex (dct_out, gmem11), Ey (bin_density,
        // gmem9), the whole forward-DCT / spectral / inverse pipeline via fft_pl (no AIE). On-chip
        // scratch; only valid in the small-grid build (GRID*GRID must fit on-chip).
        static float rho_[GRID * GRID], Ex_[GRID * GRID], Ey_[GRID * GRID];
        static float tA_[GRID * GRID], tB_[GRID * GRID];
        for (int i = 0; i < GRID * GRID; i++) {
#pragma HLS PIPELINE II=1
            rho_[i] = dct_in[i];
        }
        field_solve_pl(rho_, Ex_, Ey_, tA_, tB_);
        for (int i = 0; i < GRID * GRID; i++) {
#pragma HLS PIPELINE II=1
            dct_out[i] = Ex_[i]; bin_density[i] = Ey_[i];
        }
    }
#endif
    else if (mode == MODE_REFRESH_PINS) {
        // P2: fold v_k into both pin arrays. The net-major pass is the one real random gather
        // left in the HPWL path; the node-major one is monotone. Separate loops on purpose --
        // see refresh_net_pins / refresh_node_pins in hpwl_gradient.hpp.
        refresh_net_pins(node_pos, pin_off, pins, net_ptr[num_nets]);
        refresh_node_pins(node_pos, npin_off, npins, num_npins);
    }
    else { // MODE_HPWL_GRAD
        // dct_out[0] carries the HPWL by-product (P1b): phase 1 already forms every net's
        // bounding box, so its half-perimeter sum is free here and the host no longer needs a
        // separate pass for it. Same output slot metrics uses for HPWL, so the readback
        // contract is unchanged. The host's dummy dct_out in this mode is sizeof(float) --
        // exactly the one element written (Driver.cpp:103).
        hpwl_gradient(net_ptr, pins, npins, exp_lut, bb, sums, node_grad, dct_out,
                inv_gamma, inv_lut_step, lut_size, num_nets, num_movable, num_npins);
    }
}
} // extern "C"
