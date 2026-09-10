// DensityVerify.cpp -- see DensityVerify.hpp.
//
// Old-ABI TU (like HpwlGradVerify): builds the golden with the shared parser /
// Grid, and crosses into the new-ABI XRT Driver only through runDensityBin's
// POD + const char* boundary (PackedDesign is std::vector-only).
//
// ⚠ The golden MOVED when the host fork was merged (TODO #9, 2026-08-04). pl_algo used to
// carry its own frozen copy of Grid.cpp with NO sqrt(2) density clamp, while the PL gained
// the clamp on 2026-07-05 (node_footprint.hpp, commit 0237e57). Every `--density` run
// between those dates compared a clamped device result against an UNCLAMPED golden -- any
// recorded PASS from that window is void. The shared Grid clamps, so the two now agree on
// the smoothing. One known divergence is left, and it is NOT fixed here: the PL shifts a
// footprint that overhangs the grid back inside, the software golden does not (it relies on
// Placer::enforceDieBoundaries, which does not run in this harness). It affects only cells
// within ~sqrt(2) bins of the die edge -- see TODO #9.

#include "DensityVerify.hpp"
#include "Driver.hpp"
#include "host_interface.hpp"

#include "DataBase.h"
#include "Grid.h"
#include "Node.h"

#include <vector>
#include <cmath>
#include <cstdio>

namespace plalgo {

// sw_only maximum_utilization default; only consistency between golden and PL matters.
static constexpr float TARGET_DENSITY = 0.9f;

int runDensityVerify(AIEplace::DataBase& db, const PackedDesign& pk, const char* xclbin_path) {
    const int G = DENSITY_GRID;
    AIEplace::Box die = db.getDieArea();
    const float bin_w = (float)die.getXsize() / G;
    const float bin_h = (float)die.getYsize() / G;

    // ---- golden: shared Grid path (host/src/common). Sync probe = pos (the footprint reads the
    // probe position), then two-pass fixed + clamp + movable, fillers EXCLUDED.
    //
    AIEplace::Grid grid(die);   // 1024 x 1024

    // Geometry + deposit, split so the shared add is replayed in node order -- the deterministic
    // path, and the one that does not depend on whether this host was built with OpenMP.
    auto deposit = [&](AIEplace::Node* node_p) {
        grid.computeNodeOverlaps(node_p, /*deposit_atomically=*/false);
        grid.depositNodeOverlaps(node_p);
    };

    for (const auto& kv : db.getComponents()) kv.second->initializeState(kv.second->getPos());
    for (const auto& kv : db.getIOPads())     kv.second->initializeState(kv.second->getPos());

    for (const auto& kv : db.getComponents())               // PASS 1: fixed components
        if (kv.second->getStatus() == AIEplace::FIXED) deposit(kv.second);
    for (const auto& kv : db.getIOPads())                   // ... and IOPads (fixed)
        deposit(kv.second);
    grid.clampFixedDensity(TARGET_DENSITY);                 // CLAMP
    for (const auto& kv : db.getComponents())               // PASS 2: movable components
        if (kv.second->getStatus() != AIEplace::FIXED) deposit(kv.second);

    std::vector<std::vector<float>> rho = grid.getBinDensities();   // rho[x][y]

    // ---- device: run the PL binning module ----
    std::vector<float> dev((size_t)DENSITY_NBINS);
    runDensityBin(pk, bin_w, bin_h, TARGET_DENSITY, dev.data(), xclbin_path);

    // ---- compare rel_rms over the whole grid (x-major dev[x*G + y]) ----
    double sse = 0, ref2 = 0, max_abs = 0;
    for (int x = 0; x < G; x++)
        for (int y = 0; y < G; y++) {
            const double g = rho[x][y], a = dev[(size_t)x * G + y], d = a - g;
            sse += d * d; ref2 += g * g;
            if (std::fabs(d) > max_abs) max_abs = std::fabs(d);
        }
    const double rel_rms = (ref2 > 0) ? std::sqrt(sse / ref2) : 0.0;
    const double TOL = 1e-4;     // expect ~1e-6 (fp reassociation: golden bins in db order,
    const bool   pass = rel_rms < TOL;   // PL bins in node-index order)

    printf("[density] G=%d bin=%.4gx%.4g td=%.2f  max_abs=%.3e  rel_rms=%.3e  -> %s\n",
           G, bin_w, bin_h, TARGET_DENSITY, max_abs, rel_rms, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

} // namespace plalgo
