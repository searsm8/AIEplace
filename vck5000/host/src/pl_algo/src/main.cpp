// main.cpp -- pl_algo host, v0 bring-up.
//
// v0 (this file): parse a benchmark, pack the static design into the host->PL
// buffers, and verify the packing by comparing a CPU HPWL computed from the
// packed buffers against the DataBase golden. No XRT / PL kernel yet -- that is
// added next (Driver.cpp under BUILD_XRT), at which point the PL result joins
// this comparison chain.

#include "DataBase.h"
#include "Grid.h"
#include "Packer.hpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>

#ifdef USE_XILINX_XRT
#include "Placement.hpp"
#include "PackedDesign.hpp"

// Tiny synthetic design for a FAST hw_emu RTL-sim waveform of the HPWL gradient CU.
// A real benchmark has 10^5-10^6 pins => RTL sim would run for hours; this 6-node /
// 3-net / 9-pin case exercises all three segmented-reduction phases (A1 bbox, A2 B/C
// sums, B node gradient) in a handful of simulated cycles. Selected by passing the
// benchmark path "synthetic" to --hpwl-grad.
static plalgo::PackedDesign makeSyntheticDesign() {
    using namespace plalgo;
    PackedDesign pk;
    pk.header.num_movable = 4;   // nodes 0-3 movable
    pk.header.num_nodes   = 6;   // nodes 4-5 fixed (still carry HPWL of nets they touch)
    pk.header.num_nets    = 3;
    pk.header.num_pins    = 9;
    pk.node_pos = { {10,10}, {30,20}, {20,40}, {50,30}, {5,5}, {60,60} };
    pk.node_box = { {10,10,2,2}, {30,20,2,2}, {20,40,2,2}, {50,30,2,2}, {5,5,4,4}, {60,60,4,4} };
    pk.net_ptr  = { 0, 3, 6, 9 };                  // net0={0,1,2} net1={1,3,4} net2={2,3,5}
    auto P = [](int n, int net){ NodePin p; p.node_idx=n; p.off_x=0; p.off_y=0; p.net=net; return p; };
    pk.pins  = { P(0,0),P(1,0),P(2,0),  P(1,1),P(3,1),P(4,1),  P(2,2),P(3,2),P(5,2) };
    // NODE-major, movable pins only, sorted ascending by node_idx (pass B input)
    pk.npins = { P(0,0), P(1,0),P(1,1), P(2,0),P(2,2), P(3,1),P(3,2) };
    return pk;
}
#endif

#ifdef USE_XILINX_XRT
#include "Driver.hpp"
#include "HpwlGradVerify.hpp"
#include "DensityVerify.hpp"
#include "DCT1DVerify.hpp"
#include "TransposeVerify.hpp"
#include "FieldVerify.hpp"
#include "ForceVerify.hpp"
#include "IterVerify.hpp"
#include "MetricsVerify.hpp"
#include <cstring>
#endif

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: %s <benchmark_dir> [xclbin]\n", argv[0]);
        printf("       %s --hpwl-grad <benchmark_dir> <xclbin>\n", argv[0]);
        printf("       %s --density   <benchmark_dir> <xclbin>\n", argv[0]);
        printf("       %s --dct       <xclbin>\n", argv[0]);
        printf("       %s --dct-rowpass <xclbin>\n", argv[0]);
        printf("       %s --transpose   <xclbin>\n", argv[0]);
        printf("       %s --dct-transpose <xclbin>\n", argv[0]);
        printf("       %s --auv         <xclbin>\n", argv[0]);
        printf("       %s --idct-transpose  <xclbin>\n", argv[0]);
        printf("       %s --idxst-transpose <xclbin>\n", argv[0]);
        printf("       %s --spectral        <xclbin>\n", argv[0]);
        printf("       %s --field           <xclbin>\n", argv[0]);
        printf("       %s --force-gather    <xclbin>\n", argv[0]);
        printf("       %s --density-grad    <xclbin>\n", argv[0]);
        printf("       %s --iter-update     <xclbin>\n", argv[0]);
        printf("       %s --metrics    <benchmark_dir> <xclbin>\n", argv[0]);
        printf("       %s --place      <benchmark_dir> <xclbin> [max_iters]\n", argv[0]);
        return 1;
    }

#ifdef USE_XILINX_XRT
    // Verify the first AIE-using mode (1D DCT via the AIE FFT) on synthetic vectors.
    // No benchmark needed (inputs are synthetic).
    if (argc >= 3 && std::strcmp(argv[1], "--dct") == 0)
        return plalgo::runDCT1DVerify(argv[2]);

    // Stage 3a: verify the 8-lane row-DCT pass on a synthetic matrix.
    if (argc >= 3 && std::strcmp(argv[1], "--dct-rowpass") == 0)
        return plalgo::runDCTRowPassVerify(argv[2]);

    // Stage 3b: verify the matrix transpose (naive + tiled) on a synthetic matrix.
    if (argc >= 3 && std::strcmp(argv[1], "--transpose") == 0)
        return plalgo::runTransposeVerify(argv[2]);

    // Stage 3c: verify the fused DCT+transpose pass on a synthetic matrix.
    if (argc >= 3 && std::strcmp(argv[1], "--dct-transpose") == 0)
        return plalgo::runDctTransposeVerify(argv[2]);

    // Stage 3c composition: verify the forward 2D DCT (two fused passes) on a synthetic matrix.
    if (argc >= 3 && std::strcmp(argv[1], "--auv") == 0)
        return plalgo::runAuvVerify(argv[2]);

    // Stage 4a/4b: verify the fused inverse passes (IDCT / IDXST + transpose).
    if (argc >= 3 && std::strcmp(argv[1], "--idct-transpose") == 0)
        return plalgo::runIdctTransposeVerify(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "--idxst-transpose") == 0)
        return plalgo::runIdxstTransposeVerify(argv[2]);

    // Stage 4c: verify the spectral multiply (a_uv -> Ex_hat, Ey_hat).
    if (argc >= 3 && std::strcmp(argv[1], "--spectral") == 0)
        return plalgo::runSpectralVerify(argv[2]);

    // Stage 4d: verify the full field solve (rho -> Ex, Ey) vs compute_eField_DCT.
    if (argc >= 3 && std::strcmp(argv[1], "--field") == 0)
        return plalgo::runFieldVerify(argv[2]);

    // Stage 5a: verify the force gather (per-node density gradient) on synthetic data.
    if (argc >= 3 && std::strcmp(argv[1], "--force-gather") == 0)
        return plalgo::runForceGatherVerify(argv[2]);

    // Stage 5b: verify the full density gradient pipeline end-to-end.
    if (argc >= 3 && std::strcmp(argv[1], "--density-grad") == 0)
        return plalgo::runDensityGradientVerify(argv[2]);

    // Stage 5c.1/5c.2: verify one Nesterov step on synthetic data (no benchmark needed).
    if (argc >= 3 && std::strcmp(argv[1], "--iter-update") == 0)
        return plalgo::runIterUpdateVerify(argv[2]);
#endif

#ifdef USE_XILINX_XRT
    // Verify the PL HPWL gradient compute unit on a real benchmark: parse + pack,
    // run hpwl_CU on the device, compare per-node gradient vs the CPU golden.
    if (argc >= 4 && std::strcmp(argv[1], "--hpwl-grad") == 0) {
        plalgo::PackedDesign pk;
        if (std::strcmp(argv[2], "synthetic") == 0) {
            pk = makeSyntheticDesign();   // tiny case for a fast hw_emu waveform
            printf("[synthetic] tiny design for hw_emu waveform\n");
        } else {
            AIEplace::DataBase db(argv[2]);
            db.printInfo();
            pk = plalgo::packDesign(db);
        }
        printf("[pack] M=%d  N=%d  nets=%d  pins=%d\n",
               pk.header.num_movable, pk.header.num_nodes,
               pk.header.num_nets, pk.header.num_pins);
        return plalgo::runHpwlGradVerify(pk, argv[3]);
    }

    // Verify the PL bin-density module on a real benchmark: parse + pack, run
    // density_bin on the device, compare rho vs the sw_only Grid golden.
    if (argc >= 4 && std::strcmp(argv[1], "--density") == 0) {
        AIEplace::DataBase db(argv[2]);
        db.printInfo();
        plalgo::PackedDesign pk = plalgo::packDesign(db);
        printf("[pack] M=%d  N=%d  nets=%d  pins=%d\n",
               pk.header.num_movable, pk.header.num_nodes,
               pk.header.num_nets, pk.header.num_pins);
        return plalgo::runDensityVerify(db, pk, argv[3]);
    }

    // Stage 5c.4: verify the metrics reduce (HPWL on a real design, overflow on synthetic rho).
    if (argc >= 4 && std::strcmp(argv[1], "--metrics") == 0) {
        AIEplace::DataBase db(argv[2]);
        db.printInfo();
        plalgo::PackedDesign pk = plalgo::packDesign(db);
        printf("[pack] M=%d  N=%d  nets=%d  pins=%d\n",
               pk.header.num_movable, pk.header.num_nodes,
               pk.header.num_nets, pk.header.num_pins);
        return plalgo::runMetricsVerify(pk, argv[3]);
    }

    // Stage 5c.5/5c.6: run the full PL placement loop on a benchmark for a few iterations
    // and sanity-check the trajectory. usage: --place <bench> <xclbin> [max_iters]
    if (argc >= 4 && std::strcmp(argv[1], "--place") == 0) {
        AIEplace::DataBase db(argv[2]);
        db.printInfo();
        plalgo::PackedDesign pk = plalgo::packDesign(db);
        const int max_iters = (argc >= 5) ? std::atoi(argv[4]) : 2;

        const int   G = plalgo::DENSITY_GRID;
        AIEplace::Box die = db.getDieArea();
        const float die_x = (float)die.getXsize(), die_y = (float)die.getYsize();
        const int   N = pk.header.num_nodes, M = pk.header.num_movable;
        const int   num_nets = pk.header.num_nets;
        const int   num_pins = (int)pk.pins.size(), num_npins = (int)pk.npins.size();

        // sw_only base_gamma: gamma_bin_scaled referenced to a FIXED grid (grid-independent):
        //   base_gamma = init_gamma * (die_w + die_h) / gamma_ref_grid  (init_gamma=4, ref=512).
        // init_gamma plays XPlace's wa_coeff role; the fixed reference keeps absolute gamma the same
        // regardless of the actual grid (avoids over-sharpening at fine grids).
        const float base_gamma = 4.0f * (die_x + die_y) / 512.0f;

        // Normalized exp LUT (gamma-independent; only inv_lut_step depends on gamma).
        const int GAMMA_MULT = 12;
        const int lut_size = (int)(GAMMA_MULT / plalgo::PLACE_STEP_NORM) + 2;
        std::vector<float> lut(lut_size);
        for (int i = 0; i < lut_size; i++) lut[i] = std::exp(-(float)i * plalgo::PLACE_STEP_NORM);

        // Per-movable-node preconditioner statics: degree (#nets) and area.
        std::vector<int32_t> degree(M, 0);
        for (const auto& r : pk.pins) if (r.node_idx >= 0 && r.node_idx < M) degree[r.node_idx]++;
        std::vector<float> area(M);
        float total_mov = 0.0f;
        for (int n = 0; n < M; n++) { area[n] = pk.node_box[n].w * pk.node_box[n].h; total_mov += area[n]; }
        const float avg_area = total_mov / std::max(1, M);

        // target_density from the benchmark's placement.constraints (maximum_utilization); ISPD2005
        // has no constraints file -> default 1.0 (matches sw_only and XPlace ispd2005).
        const float target_density = db.getMaximumUtilization() > 0.0f
                                   ? db.getMaximumUtilization() : 1.0f;
        plalgo::PlacementConfig cfg{};
        cfg.max_iters = max_iters; cfg.die_x = die_x; cfg.die_y = die_y;
        cfg.bin_w = die_x / G; cfg.bin_h = die_y / G; cfg.target_density = target_density;
        cfg.base_gamma = base_gamma; cfg.gamma_schedule = 1;   // sw_only enables the overflow-driven gamma schedule
        cfg.init_step_length = 0.01f; cfg.density_weight_init_multiplier = 8e-5f;
        cfg.enable_momentum = 1;
        cfg.density_weight_min_step = 0.95f; cfg.density_weight_max_step = 1.05f;
        cfg.overflow_threshold = 0.07f; cfg.min_iters = 50; cfg.conv_iters = 30;
        // NB: the PL density datapath is a FIXED GRID (DENSITY_GRID=1024). sw_only's ePlace auto
        // grid sizing is a host decision; on the PL the grid is pinned by the hardware, so the grid
        // formula does not apply here (documented in report_pl_port.md).

        printf("[place] bench M=%d N=%d nets=%d die=%.1fx%.1f bin=%.4gx%.4g gamma=%.4g max_iters=%d\n",
               M, N, num_nets, die_x, die_y, cfg.bin_w, cfg.bin_h, base_gamma, max_iters);

        std::vector<float> hpwl_hist(max_iters, 0.0f), ovfl_hist(max_iters, 0.0f);
        std::vector<plalgo::coord_t> final_pos(M);
        const int ran = plalgo::runPlacement(cfg, N, M, num_nets, num_pins, num_npins,
            pk.node_pos.data(), pk.node_box.data(), pk.net_ptr.data(), pk.pins.data(),
            pk.npins.data(), lut.data(), lut_size, degree.data(), area.data(), avg_area,
            hpwl_hist.data(), ovfl_hist.data(), final_pos.data(), argv[3]);

        // Sanity: loop ran, final positions finite and inside the die (proves the loop closes
        // correctly). Note ePlace HPWL rises early as cells spread; overflow should fall.
        bool ok = ran > 0;
        for (int n = 0; n < M && ok; n++) {
            if (!std::isfinite(final_pos[n].x) || !std::isfinite(final_pos[n].y)) ok = false;
            if (final_pos[n].x < -1.0f || final_pos[n].y < -1.0f ||
                final_pos[n].x > die_x + 1.0f || final_pos[n].y > die_y + 1.0f) ok = false;
        }
        printf("[place] HPWL   trajectory:");
        for (int i = 0; i < ran; i++) printf(" %.5g", hpwl_hist[i]);
        printf("\n[place] overflow trajectory:");
        for (int i = 0; i < ran; i++) printf(" %.4f", ovfl_hist[i]);
        const bool ovfl_drop = ran >= 2 && ovfl_hist[ran-1] <= ovfl_hist[0];
        printf("\n[place] %d iters run; final positions %s; overflow %s -> %s\n",
               ran, ok ? "finite/in-bounds" : "BAD",
               ovfl_drop ? "decreasing" : "(not strictly decreasing over this window)",
               ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
#endif

    // Parse LEF/DEF or bookshelf (full parse happens in the constructor).
    AIEplace::DataBase db(argv[1]);
    db.printInfo();

    // Stage the v0 host->PL buffers.
    plalgo::PackedDesign pk = plalgo::packDesign(db);
    printf("[pack] M=%d  N=%d  nets=%d  pins=%d\n",
           pk.header.num_movable, pk.header.num_nodes,
           pk.header.num_nets, pk.header.num_pins);

    // Verify the packing: HPWL recomputed from the packed buffers must match a
    // golden recomputed from the DataBase. Both summed in double -- at ~1e6 nets
    // a float accumulator is order-dependent to ~0.3%, so it cannot serve as a
    // reference (and the PL kernel accumulates in double for the same reason).
    double golden = 0.0;
    for (AIEplace::Net* net : db.getNetsVector()) {
        const int deg = net->getDegree();               // XPlace net_mask: 2 <= deg <= IGNORE_NET_DEGREE
        if (deg <= 1 || deg > plalgo::IGNORE_NET_DEGREE) continue;
        golden += (double)net->computeWirelength_HPWL();
    }
    const double packed  = plalgo::hpwlFromPacked(pk);
    const double rel_err = std::fabs(packed - golden) / std::fabs(golden);
    printf("[hpwl] golden=%.10g  packed=%.10g  rel_err=%.3e  -> %s\n",
           golden, packed, rel_err, rel_err < 1e-6 ? "PASS" : "FAIL");

#ifdef USE_XILINX_XRT
    // If an xclbin is given, run the kernel on the device and compare its HPWL
    // (float, accumulated in double) against the golden.
    if (argc >= 3) {
        const double device  = (double)plalgo::runHpwlKernel(pk, argv[2]);
        const double rel_dev = std::fabs(device - golden) / std::fabs(golden);
        printf("[hpwl/PL] device=%.10g  golden=%.10g  rel_err=%.3e  -> %s\n",
               device, golden, rel_dev, rel_dev < 1e-5 ? "PASS" : "FAIL");
        return rel_dev < 1e-5 ? 0 : 1;
    }
#endif

    return rel_err < 1e-6 ? 0 : 1;
}
