#ifndef PL_ALGO_ITER_VERIFY_HPP
#define PL_ALGO_ITER_VERIFY_HPP

// IterVerify -- Stage 5c: verify one Nesterov step (iteration_update -> memory_writer)
// against a double-precision golden that replicates sw_only combineGradients + Node::step
// + enforceDieBoundaries. Synthetic random inputs; no benchmark needed.

namespace plalgo {
int runIterUpdateVerify(const char* xclbin_path);
}

#endif // PL_ALGO_ITER_VERIFY_HPP
