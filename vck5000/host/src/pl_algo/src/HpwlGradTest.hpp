#ifndef PL_ALGO_HPWL_GRAD_TEST_HPP
#define PL_ALGO_HPWL_GRAD_TEST_HPP

// HpwlGradTest -- Milestone B end-to-end check (THROWAWAY scaffolding).
//
// Builds a synthetic net-group packet in the AIE format, runs it through top +
// the AIE hpwl_grad graph (via Driver::runHpwlGradPacket), and compares the
// device partials against a pinned-gamma (AIE_INV_GAMMA) CPU golden with a
// tolerance (fast_exp is approximate). This is a fixture to prove the
// host->PL->AIE->PL->host path, NOT the product packer -- it is deleted once the
// PL packer lands in hpwl_manager.

namespace plalgo {

// Returns 0 on PASS, 1 on FAIL.
int runHpwlGradTest(const char* xclbin_path);

} // namespace plalgo

#endif // PL_ALGO_HPWL_GRAD_TEST_HPP
