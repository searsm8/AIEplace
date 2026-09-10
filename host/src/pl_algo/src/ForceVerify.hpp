#ifndef PL_ALGO_FORCE_VERIFY_HPP
#define PL_ALGO_FORCE_VERIFY_HPP

// ForceVerify.hpp -- Stage 5a: verify the force gather (per-node density gradient) on
// synthetic node boxes + a synthetic E-field. Golden = the same overlap-area-weighted field
// sum computed in double (the adjoint of density_bin's scatter). Returns 0 on PASS.

namespace plalgo {

int runForceGatherVerify(const char* xclbin_path);

// Stage 5b: end-to-end density gradient (density_bin -> 2D DCT -> spectral -> inverse ->
// force_gather) vs a scatter->naive-field->gather golden. Returns 0 on PASS.
int runDensityGradientVerify(const char* xclbin_path);

} // namespace plalgo

#endif // PL_ALGO_FORCE_VERIFY_HPP
