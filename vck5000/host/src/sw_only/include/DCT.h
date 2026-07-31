/**
 * @file DCT.h
 * @brief 1D DCT/IDCT/IDXST transforms used by the electrostatic density solver — both a
 *        naive O(N^2) reference and an O(N log N) FFT-based implementation (Makhoul).
 */
#pragma once
#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN

// Inputs are taken by const reference: at a 1024x1024 grid a by-value matrix parameter
// copied ~4 MB per call, and compute_eField_DCT alone calls transpose 6x per iteration.
std::vector< std::vector<float> > transpose   (const std::vector< std::vector<float> >& input);

std::vector<float> DCT_naive   (const std::vector<float>& input);
std::vector<float> IDCT_naive  (const std::vector<float>& input);
std::vector<float> IDXST_naive (const std::vector<float>& input);

/*
 * The FFT forms take raw pointers and a caller-owned output buffer instead of returning a
 * vector. They run 6N times per placement iteration (2N forward in compute_a_uv_DCT, 4N inverse
 * in compute_eField_DCT), and returning a vector allocated two heap blocks per call — ~25k
 * allocations per iteration at a 2048 grid, which is exactly the allocator traffic that would
 * stop the row loops from scaling once they are threaded.
 *
 * @p out may alias @p in: every input element is consumed into scratch before the first write.
 * Safe to call concurrently on different rows — the scratch buffers are thread_local and the
 * twiddle tables are built once per N under a lock, then read-only.
 */
void DCT_fft   (const float* in, float* out, int N, bool normalize = false);
void IDCT_fft  (const float* in, float* out, int N, bool normalize = false);
void IDXST_fft (const float* in, float* out, int N, bool normalize = false);

AIEPLACE_NAMESPACE_END