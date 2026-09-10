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
#include <cstring>
#include <vector>
#include <algorithm>

#ifdef USE_XILINX_XRT
#include "Placement.hpp"
#include "PackedDesign.hpp"
#include <random>
#ifdef PL_FIELD_SOLVE
#include "modules/field_solve_pl.hpp"   // C golden for the --field-solve RTL-vs-Csim check
#endif

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
    auto P = [](int n, int net){ NodePin p; p.node_idx=n; p.x=0; p.y=0; p.net=net; return p; };
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

// --pack-check <bench>: exercise the filler + movable-macro packer on a real design and assert the
// nested-range invariants (host-only, no XRT/device -- runs in the CPU-only build). This is the
// tier-1-style verification of tagMovableMacros + DataBase::addFillers + packDesign's bucketing.
static int runPackCheck(const char* bench) {
    AIEplace::DataBase db(bench);
    db.printInfo();
    float target = db.getMaximumUtilization() > 0.0f ? db.getMaximumUtilization() : 1.0f;

    plalgo::tagMovableMacros(db);
    const float eff_td = db.addFillers(target);          // may raise target; populates getFillers()
    plalgo::PackedDesign pk = plalgo::packDesign(db);

    const int M = pk.header.num_movable, N = pk.header.num_nodes;
    const int fm = pk.header.first_macro, ff = pk.header.first_filler;
    const int n_std = fm, n_macro = ff - fm, n_filler = M - ff;
    bool ok = true;

    // [1] nested ranges are ordered and partition the movable prefix; filler count matches getFillers.
    if (!(0 <= fm && fm <= ff && ff <= M && M <= N)) {
        printf("FAIL [1] range order  0<=%d<=%d<=%d<=%d violated\n", fm, ff, M, N); ok = false;
    }
    if (n_filler != (int)db.getFillers().size()) {
        printf("FAIL [1] packed filler count %d != db.getFillers() %d\n", n_filler, (int)db.getFillers().size());
        ok = false;
    }

    // [2] every filler sits inside the die bounding box (uniform-random placement stayed in-die).
    AIEplace::Box die = db.getDieArea();
    const float llx = die.getPosBottomLeft().x, lly = die.getPosBottomLeft().y;
    const float hix = llx + die.getXsize(),     hiy = lly + die.getYsize();
    int oob = 0;
    for (int n = ff; n < M; n++) {
        const plalgo::coord_t& p = pk.node_pos[n];
        if (p.x < llx || p.x > hix || p.y < lly || p.y > hiy) oob++;
    }
    if (oob) { printf("FAIL [2] %d filler positions outside the die box\n", oob); ok = false; }

    // [3] fillers are on no nets, so the HPWL over real nodes must still match the DataBase golden.
    double golden = 0.0;
    for (AIEplace::Net* net : db.getNetsVector()) {
        const int deg = net->getDegree();
        if (deg <= 1 || deg > plalgo::IGNORE_NET_DEGREE) continue;
        golden += (double)net->computeWirelength_HPWL();
    }
    const double packed = plalgo::hpwlFromPacked(pk);
    const double rel = std::fabs(packed - golden) / std::fabs(golden);
    if (!(rel < 1e-6)) { printf("FAIL [3] HPWL rel_err=%.3e (fillers must not perturb net HPWL)\n", rel); ok = false; }

    printf("[pack-check] N=%d M=%d  std=%d macro=%d filler=%d  eff_td=%.4g\n", N, M, n_std, n_macro, n_filler, eff_td);
    printf("[pack-check] HPWL golden=%.6g packed=%.6g rel=%.3e  (fillers in-die: %d oob)\n", golden, packed, rel, oob);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

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
        printf("       %s --resident-place <benchmark_dir> <xclbin> [max_iters]\n", argv[0]);
        printf("       %s --pack-check <benchmark_dir>\n", argv[0]);
        return 1;
    }

    // Host-only packer check (fillers + macro tags + nested ranges); no device, CPU-only build OK.
    if (argc >= 3 && std::strcmp(argv[1], "--pack-check") == 0)
        return runPackCheck(argv[2]);

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
#ifdef PL_FIELD_SOLVE
    // PL-only field solve (fft_pl) in hw_emu: random rho -> Ex, Ey on device; compare to the
    // C field_solve_pl (RTL-vs-Csim). Small-grid build (-DPL_GRID=<N> -DPL_FIELD_SOLVE).
    if (argc >= 3 && std::strcmp(argv[1], "--field-solve") == 0) {
        const int N = plalgo::DENSITY_GRID, NB = N * N;
        std::vector<float> rho(NB), Ex(NB), Ey(NB), ExG(NB), EyG(NB), tA(NB), tB(NB);
        std::mt19937 rng(2024); std::uniform_real_distribution<float> uni(0.f, 1.f);
        for (int i = 0; i < NB; i++) rho[i] = uni(rng);
        plalgo::field_solve_pl(rho.data(), ExG.data(), EyG.data(), tA.data(), tB.data()); // C golden
        plalgo::runFieldSolvePl(rho.data(), Ex.data(), Ey.data(), argv[2]);                // device
        double ee = 0, en = 0, ye = 0, yn = 0;
        for (int i = 0; i < NB; i++) {
            double dx = Ex[i] - ExG[i], dy = Ey[i] - EyG[i];
            ee += dx * dx; en += (double)ExG[i] * ExG[i]; ye += dy * dy; yn += (double)EyG[i] * EyG[i];
        }
        double rex = std::sqrt(ee / (en + 1e-30)), rey = std::sqrt(ye / (yn + 1e-30));
        bool ok = rex < 1e-4 && rey < 1e-4;
        printf("[field-solve] N=%d  Ex rel_rms=%.3e  Ey rel_rms=%.3e  -> %s\n",
               N, rex, rey, ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    // ONE full density-driven iteration on the PL (density_bin -> field_solve_pl -> force_gather ->
    // iteration_update), for a full-iteration hw_emu waveform. Small synthetic design on the grid.
    if (argc >= 3 && std::strcmp(argv[1], "--one-iter") == 0) {
        const int G = plalgo::DENSITY_GRID;      // grid = die (bin_w=bin_h=1)
        const int M = 6;                          // movable nodes
        const float die = (float)G;
        std::vector<plalgo::NodeBox> box(M);
        std::vector<plalgo::coord_t> u_k(M), u_out(M), v_out(M), gden(M);
        std::vector<float> prec(M, 1.0f);
        std::mt19937 rng(7); std::uniform_real_distribution<float> pos(4.f, die - 8.f), sz(2.f, 5.f);
        for (int n = 0; n < M; n++) {             // clustered near center -> density force spreads them
            box[n].x = die * 0.5f + (float)(n - 3) * 1.5f; box[n].y = die * 0.5f + (float)(n % 2) * 1.5f;
            box[n].w = sz(rng); box[n].h = sz(rng);
            u_k[n].x = box[n].x; u_k[n].y = box[n].y;
        }
        printf("[one-iter] M=%d grid=%d  running density_bin -> field_solve_pl -> force_gather -> iteration_update\n", M, G);
        plalgo::runOneIterationPl(box.data(), u_k.data(), prec.data(), M, M, 1.0f, 1.0f, 1.0f,
                                  /*lambda*/0.5f, /*alpha*/0.3f, /*coeff*/0.0f, die, die,
                                  u_out.data(), v_out.data(), gden.data(), argv[2]);
        double moved = 0; for (int n = 0; n < M; n++) { double dx = v_out[n].x - u_k[n].x, dy = v_out[n].y - u_k[n].y; moved += std::sqrt(dx*dx+dy*dy); }
        printf("[one-iter] done. mean |g_density|=%.3e  mean node move=%.3f  (node0 v: %.2f,%.2f -> %.2f,%.2f)\n",
               [&]{ double s=0; for(int n=0;n<M;n++) s+=std::sqrt((double)gden[n].x*gden[n].x+(double)gden[n].y*gden[n].y); return s/M; }(),
               moved / M, u_k[0].x, u_k[0].y, v_out[0].x, v_out[0].y);
        return 0;
    }
#endif
#endif

#ifdef USE_XILINX_XRT
    // Verify the PL HPWL gradient compute unit on a real benchmark: parse + pack,
    // run hpwl_gradient on the device, compare per-node gradient vs the CPU golden.
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
        // target_density from the benchmark's placement.constraints (maximum_utilization); ISPD2005
        // has no constraints file -> default 1.0 (matches sw_only and XPlace ispd2005).
        float target_density = db.getMaximumUtilization() > 0.0f
                             ? db.getMaximumUtilization() : 1.0f;
        // Fillers + macro tags before packing, mirroring sw_only setupDesign: tag movable macros,
        // then add fillers (addFillers may RAISE target_density when the std-cell utilization exceeds
        // the request -- adopt the returned effective value). packDesign then buckets std|macro|filler.
        plalgo::tagMovableMacros(db);
        target_density = db.addFillers(target_density);
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
        for (int n = 0; n < M; n++) area[n] = pk.node_box[n].w * pk.node_box[n].h;

        // target_density was resolved above (benchmark maximum_utilization, then addFillers may
        // have raised it to the std-cell utilization). Adopt that effective value here.
        plalgo::PlacementConfig cfg{};
        cfg.max_iters = max_iters; cfg.die_x = die_x; cfg.die_y = die_y;
        cfg.bin_w = die_x / G; cfg.bin_h = die_y / G; cfg.target_density = target_density;
        cfg.base_gamma = base_gamma; cfg.gamma_schedule = 1;   // sw_only enables the overflow-driven gamma schedule
        cfg.init_step_seed = 0.01f; cfg.density_weight_init_multiplier = 8e-5f;
        // Mirrors sw_only estimateInitialStep: the seed is in site widths, so it needs the design's
        // DBU-per-site. Row height is the fallback when the input names no site (TODO #23).
        cfg.site_width = db.getSiteWidth() > 0.0f ? db.getSiteWidth() : db.getRowHeight();
        cfg.enable_momentum = 1;
        cfg.density_weight_min_step = 0.95f; cfg.density_weight_max_step = 1.05f;
        cfg.overflow_threshold = 0.07f; cfg.min_iters = 50; cfg.conv_iters = 30;
        // NB: the PL density datapath is a FIXED GRID (DENSITY_GRID=1024). sw_only's ePlace auto
        // grid sizing is a host decision; on the PL the grid is pinned by the hardware, so the grid
        // formula does not apply here (documented in report_pl_port.md).

        printf("[place] bench M=%d N=%d nets=%d die=%.1fx%.1f bin=%.4gx%.4g gamma=%.4g max_iters=%d\n",
               M, N, num_nets, die_x, die_y, cfg.bin_w, cfg.bin_h, base_gamma, max_iters);
        printf("[place] movable split: std=%d macro=%d filler=%d  td=%.4g\n",
               pk.header.first_macro, pk.header.first_filler - pk.header.first_macro,
               M - pk.header.first_filler, target_density);

        std::vector<float> hpwl_hist(max_iters, 0.0f), ovfl_hist(max_iters, 0.0f);
        std::vector<plalgo::coord_t> final_pos(M);
        const int ran = plalgo::runPlacement(cfg, N, M, num_nets, num_pins, num_npins,
            pk.node_pos.data(), pk.node_box.data(), pk.net_ptr.data(), pk.pins.data(),
            pk.npins.data(), lut.data(), lut_size, degree.data(), area.data(),
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

    // Stage 5 proper (#20 step 6): the DEVICE-RESIDENT loop -- ONE MODE_PLACE call runs the whole GP
    // loop on the PL. Same setup as --place; reports the final HPWL/overflow + how far cells moved.
    // usage: --resident-place <bench> <xclbin> [max_iters].  max_iters small (default 5) so the loop
    // cannot stop early (the AIE FFT run count is fixed to the passes streamed).
    if (argc >= 4 && std::strcmp(argv[1], "--resident-place") == 0) {
        AIEplace::DataBase db(argv[2]);
        db.printInfo();
        float target_density = db.getMaximumUtilization() > 0.0f ? db.getMaximumUtilization() : 1.0f;
        plalgo::tagMovableMacros(db);
        target_density = db.addFillers(target_density);
        plalgo::PackedDesign pk = plalgo::packDesign(db);
        const int max_iters = (argc >= 5) ? std::atoi(argv[4]) : 5;

        const int   G = plalgo::DENSITY_GRID;
        AIEplace::Box die = db.getDieArea();
        const float die_x = (float)die.getXsize(), die_y = (float)die.getYsize();
        const int   N = pk.header.num_nodes, M = pk.header.num_movable;
        const int   num_nets = pk.header.num_nets;
        const int   num_pins = (int)pk.pins.size(), num_npins = (int)pk.npins.size();
        const float base_gamma = 4.0f * (die_x + die_y) / 512.0f;   // gamma_bin_scaled (main --place)
        const int   lut_size = (int)(12 / plalgo::PLACE_STEP_NORM) + 2;
        std::vector<float> lut(lut_size);
        for (int i = 0; i < lut_size; i++) lut[i] = std::exp(-(float)i * plalgo::PLACE_STEP_NORM);
        std::vector<float> area(M);
        for (int n = 0; n < M; n++) area[n] = pk.node_box[n].w * pk.node_box[n].h;

        plalgo::PlacementConfig cfg{};
        cfg.max_iters = max_iters; cfg.die_x = die_x; cfg.die_y = die_y;
        cfg.bin_w = die_x / G; cfg.bin_h = die_y / G; cfg.target_density = target_density;
        cfg.base_gamma = base_gamma; cfg.gamma_schedule = 1;
        cfg.init_step_seed = 0.01f; cfg.density_weight_init_multiplier = 8e-5f;
        cfg.site_width = db.getSiteWidth() > 0.0f ? db.getSiteWidth() : db.getRowHeight();
        cfg.enable_momentum = 1; cfg.density_weight_min_step = 0.95f; cfg.density_weight_max_step = 1.05f;
        cfg.overflow_threshold = 0.07f; cfg.min_iters = 50; cfg.conv_iters = 30;

        printf("[resident] M=%d N=%d nets=%d die=%.1fx%.1f gamma=%.4g max_iters=%d  std=%d macro=%d filler=%d td=%.4g\n",
               M, N, num_nets, die_x, die_y, base_gamma, max_iters, pk.header.first_macro,
               pk.header.first_filler - pk.header.first_macro, M - pk.header.first_filler, target_density);

        std::vector<float> status(4, 0.0f);
        std::vector<plalgo::coord_t> final_pos(M);
        const int ran = plalgo::runResidentPlacement(cfg, N, M, num_nets, num_pins, num_npins,
            pk.header.first_macro, pk.header.first_filler,
            pk.node_pos.data(), pk.node_box.data(), pk.net_ptr.data(), pk.pins.data(), pk.npins.data(),
            pk.pin_off.data(), pk.npin_off.data(), pk.pin_to_npin.data(),
            lut.data(), lut_size, area.data(), max_iters,
            status.data(), final_pos.data(), argv[3]);

        double moved = 0.0; float mov_area = 0.0f;
        for (int n = 0; n < M; n++) mov_area += area[n];
        bool ok = ran > 0;
        for (int n = 0; n < M; n++) {
            if (!std::isfinite(final_pos[n].x) || !std::isfinite(final_pos[n].y)) ok = false;
            const double dx = final_pos[n].x - pk.node_pos[n].x, dy = final_pos[n].y - pk.node_pos[n].y;
            moved += std::sqrt(dx*dx + dy*dy);
        }
        const float ovf = status[1] * (cfg.bin_w * cfg.bin_h) / (mov_area + 1e-8f);
        printf("[resident] ran=%d iters, stop=%d, final HPWL=%.6g overflow=%.4f  mean node move=%.3f\n",
               ran, (int)status[3], status[0], ovf, moved / std::max(1, M));
        printf("[resident] %s\n", ok ? "PASS (finite; cells moved)" : "FAIL");
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
