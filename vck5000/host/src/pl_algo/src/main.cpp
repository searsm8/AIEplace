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

    // Verify: HPWL from packed buffers vs DataBase golden.
    const float hpwl_golden = db.computeTotalWirelength("HPWL");
    const float hpwl_packed = plalgo::hpwlFromPacked(pk);
    const float rel_err = (hpwl_golden != 0.0f)
                        ? std::fabs(hpwl_packed - hpwl_golden) / std::fabs(hpwl_golden)
                        : std::fabs(hpwl_packed);
    printf("[hpwl] golden=%.6g  packed=%.6g  rel_err=%.3e  -> %s\n",
           hpwl_golden, hpwl_packed, rel_err,
           rel_err < 1e-4f ? "PASS" : "FAIL");

    return rel_err < 1e-4f ? 0 : 1;
}
