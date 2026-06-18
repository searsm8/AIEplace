#ifndef PL_ALGO_HPWL_GRAD_VERIFY_HPP
#define PL_ALGO_HPWL_GRAD_VERIFY_HPP

// HpwlGradVerify -- verify the PL HPWL gradient compute unit (hpwl_CU) against a
// CPU golden on a real packed design.
//
// Builds the exp LUT, picks gamma from the design's coordinate span, runs the PL
// kernel (Driver::runHpwlGradCU), computes the exact-exp full-WA golden (the same
// math as markv1 computeHpwlPartials_CPU), and compares per movable node with a
// tolerance (the LUT is approximate). Operates on a PackedDesign -- parser-free.

#include "PackedDesign.hpp"

namespace plalgo {

// Returns 0 on PASS, 1 on FAIL.
int runHpwlGradVerify(const PackedDesign& pk, const char* xclbin_path);

} // namespace plalgo

#endif // PL_ALGO_HPWL_GRAD_VERIFY_HPP
