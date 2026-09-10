#ifndef PL_ALGO_DCT1D_VERIFY_HPP
#define PL_ALGO_DCT1D_VERIFY_HPP

// DCT1DVerify.hpp -- verify the first AIE-using mode (MODE_DCT_1D) on synthetic
// vectors: stage 0 (raw AIE FFT) vs a host reference forward FFT -- isolates the
// AIE graph + PL<->AIE streams + AIE-using build; stage 1 (PL shuffle + AIE FFT +
// PL twiddle/Re) vs DCT_naive -- the same golden Stage 0's math model used.
// Needs no benchmark (inputs are synthetic). Returns 0 on PASS.

namespace plalgo {

int runDCT1DVerify(const char* xclbin_path);

// Stage 3a: verify the 8-lane row-DCT pass (MODE_DCT_ROWPASS) -- DCT every row of a
// synthetic matrix through the AIE FFT pool, compare each row vs DCT_naive. Returns 0
// on PASS.
int runDCTRowPassVerify(const char* xclbin_path);

// Stage 3c: verify the fused DCT+transpose pass (MODE_DCT_TRANSPOSE) -- DCT every row of
// a synthetic N x N matrix through the AIE FFT pool, written transposed; compare vs
// transpose(DCT_naive). Returns 0 on PASS.
int runDctTransposeVerify(const char* xclbin_path);

// Stage 3c composition: verify the forward 2D DCT (two fused passes) on a synthetic N x N
// matrix; compare vs the separable reference C*rho*C^T. Returns 0 on PASS.
int runAuvVerify(const char* xclbin_path);

} // namespace plalgo

#endif // PL_ALGO_DCT1D_VERIFY_HPP
