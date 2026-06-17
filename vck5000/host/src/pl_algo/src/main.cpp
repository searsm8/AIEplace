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
#endif

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: %s <benchmark_dir>\n", argv[0]);
        return 1;
    }

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
