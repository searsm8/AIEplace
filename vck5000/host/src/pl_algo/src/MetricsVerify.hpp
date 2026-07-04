#ifndef PL_ALGO_METRICS_VERIFY_HPP
#define PL_ALGO_METRICS_VERIFY_HPP

// MetricsVerify -- Stage 5c: verify the metrics reduce. HPWL is checked on a real packed
// design against hpwlFromPacked (the same double golden main.cpp uses); overflow_sum is
// checked against a double reduce of a synthetic rho. Needs a benchmark for the HPWL half.

#include "PackedDesign.hpp"

namespace plalgo {
int runMetricsVerify(const PackedDesign& pk, const char* xclbin_path);
}

#endif // PL_ALGO_METRICS_VERIFY_HPP
