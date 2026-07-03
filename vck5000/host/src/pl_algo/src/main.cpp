// main.cpp -- pl_algo host, v0 bring-up.
//
// v0 (this file): parse a benchmark, pack the static design into the host->PL
// buffers, and verify the packing by comparing a CPU HPWL computed from the
// packed buffers against the DataBase golden. No XRT / PL kernel yet -- that is
// added next (Driver.cpp under BUILD_XRT), at which point the PL result joins
// this comparison chain.

#include "DataBase.h"
#include "Packer.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

#ifdef USE_XILINX_XRT
#include "Driver.hpp"
#include "HpwlGradVerify.hpp"
#include "DensityVerify.hpp"
#include "DCT1DVerify.hpp"
#include "TransposeVerify.hpp"
#include "FieldVerify.hpp"
#include "ForceVerify.hpp"
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
#endif

#ifdef USE_XILINX_XRT
    // Verify the PL HPWL gradient compute unit on a real benchmark: parse + pack,
    // run hpwl_CU on the device, compare per-node gradient vs the CPU golden.
    if (argc >= 4 && std::strcmp(argv[1], "--hpwl-grad") == 0) {
        AIEplace::DataBase db(argv[2]);
        db.printInfo();
        plalgo::PackedDesign pk = plalgo::packDesign(db);
        printf("[pack] M=%d  N=%d  nets=%d  pins=%d\n",
               pk.header.num_movable, pk.header.num_nodes,
               pk.header.num_nets, pk.header.num_pins);
        return plalgo::runHpwlGradVerify(pk, argv[3]);
    }

    // Verify the PL bin-density module on a real benchmark: parse + pack, run
    // density_bin on the device, compare rho vs the markv1 Grid golden.
    if (argc >= 4 && std::strcmp(argv[1], "--density") == 0) {
        AIEplace::DataBase db(argv[2]);
        db.printInfo();
        plalgo::PackedDesign pk = plalgo::packDesign(db);
        printf("[pack] M=%d  N=%d  nets=%d  pins=%d\n",
               pk.header.num_movable, pk.header.num_nodes,
               pk.header.num_nets, pk.header.num_pins);
        return plalgo::runDensityVerify(db, pk, argv[3]);
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
    for (AIEplace::Net* net : db.getNetsVector())
        golden += (double)net->computeWirelength_HPWL();
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
