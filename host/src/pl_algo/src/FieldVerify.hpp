#ifndef PL_ALGO_FIELD_VERIFY_HPP
#define PL_ALGO_FIELD_VERIFY_HPP

// FieldVerify.hpp -- Stage 4 verification: the inverse field solve. Each verify runs a PL
// pass (or the full pipeline) on synthetic data and compares against a naive double-precision
// reference (the same DCT/IDCT/IDXST convention the Stage 0 model proved == compute_eField_DCT).
// Kept separate from DCT1DVerify (the frozen Stage 3 harness). Each returns 0 on PASS.

namespace plalgo {

// 4a: fused IDCT+transpose pass vs transpose(IDCT_naive rows).
int runIdctTransposeVerify(const char* xclbin_path);

// 4b: fused IDXST+transpose pass vs transpose(IDXST_naive rows).
int runIdxstTransposeVerify(const char* xclbin_path);

// 4c: spectral multiply (both fields) vs the golden Ex_hat/Ey_hat formula.
int runSpectralVerify(const char* xclbin_path);

// 4d: full field solve rho -> Ex, Ey vs the naive compute_eField_DCT pipeline.
int runFieldVerify(const char* xclbin_path);

} // namespace plalgo

#endif // PL_ALGO_FIELD_VERIFY_HPP
