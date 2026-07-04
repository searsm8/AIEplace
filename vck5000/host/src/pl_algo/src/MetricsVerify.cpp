// MetricsVerify.cpp -- see MetricsVerify.hpp.
//
// Old-ABI TU: the goldens are computed here in double; the XRT Driver is reached through
// runMetrics' POD boundary. HPWL golden = hpwlFromPacked(pk) (the same reference main.cpp
// uses to validate the packing). overflow_sum golden = sum_bins max(0, rho - target) over
// a synthetic rho (the field/density stages are verified elsewhere; here we only check the
// reduce arithmetic and the target subtraction, which drive convergence).

#include "MetricsVerify.hpp"
#include "Packer.hpp"
#include "Driver.hpp"
#include "host_interface.hpp"

#include <vector>
#include <cmath>
#include <cstdio>
#include <random>

namespace plalgo {

int runMetricsVerify(const PackedDesign& pk, const char* xclbin_path) {
    const int num_nodes = pk.header.num_nodes;
    const int num_nets  = pk.header.num_nets;
    const int num_pins  = (int)pk.pins.size();
    const float target  = 0.9f;

    // Synthetic rho for the overflow reduce: mostly-empty grid with scattered overflow.
    std::mt19937 rng(71);
    std::uniform_real_distribution<float> rho_dist(0.0f, 1.4f);
    std::vector<float> rho((size_t)DENSITY_NBINS);
    for (auto& r : rho) r = rho_dist(rng);

    double ovfl_golden = 0.0;
    for (float r : rho) { const double e = (double)r - target; if (e > 0.0) ovfl_golden += e; }
    const double hpwl_golden = hpwlFromPacked(pk);

    printf("[metrics] verify: nodes=%d nets=%d pins=%d bins=%d target=%.2f\n",
           num_nodes, num_nets, num_pins, DENSITY_NBINS, target);

    float hpwl_dev = 0.0f, ovfl_dev = 0.0f;
    runMetrics(pk.node_pos.data(), pk.net_ptr.data(), pk.pins.data(),
               num_nodes, num_nets, num_pins, rho.data(), target,
               &hpwl_dev, &ovfl_dev, xclbin_path);

    const double hpwl_err = std::fabs((double)hpwl_dev - hpwl_golden) / (hpwl_golden + 1e-30);
    const double ovfl_err = std::fabs((double)ovfl_dev - ovfl_golden) / (ovfl_golden + 1e-30);
    const bool   ok = hpwl_err < 1e-4 && ovfl_err < 1e-4;
    printf("[metrics] HPWL: dev=%.10g golden=%.10g rel_err=%.3e\n",
           (double)hpwl_dev, hpwl_golden, hpwl_err);
    printf("[metrics] overflow_sum: dev=%.10g golden=%.10g rel_err=%.3e  -> %s\n",
           (double)ovfl_dev, ovfl_golden, ovfl_err, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace plalgo
