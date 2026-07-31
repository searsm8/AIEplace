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

std::vector<float> DCT_fft     (const std::vector<float>& input, bool normalize = false);
std::vector<float> IDCT_fft    (const std::vector<float>& input, bool normalize = false);
std::vector<float> IDXST_fft   (const std::vector<float>& input, bool normalize = false);

AIEPLACE_NAMESPACE_END