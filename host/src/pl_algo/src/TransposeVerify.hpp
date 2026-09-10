#ifndef PL_ALGO_TRANSPOSE_VERIFY_HPP
#define PL_ALGO_TRANSPOSE_VERIFY_HPP

// TransposeVerify.hpp -- Stage 3b: verify BOTH transpose variants (naive + tiled)
// bit-exact against a host transpose on a synthetic matrix. A transpose is pure data
// movement, so correct == identical (mismatches must be 0). Bandwidth (burst
// inference, efficiency) is compared separately in the C-synth report / hw_emu.

namespace plalgo {

int runTransposeVerify(const char* xclbin_path);

} // namespace plalgo

#endif // PL_ALGO_TRANSPOSE_VERIFY_HPP
