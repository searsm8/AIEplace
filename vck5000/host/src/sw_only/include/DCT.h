/**
 * @file DCT.h
 * @brief 1D DCT/IDCT/IDXST transforms used by the electrostatic density solver — both a
 *        naive O(N^2) reference and an O(N log N) FFT-based implementation (Makhoul).
 */
#pragma once
#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN

// TODO: These functions should be operating on the parameter passed by reference, not copying large data structures!
std::vector< std::vector<float> > transpose   (std::vector< std::vector<float> > input);

std::vector<float> DCT_naive   (std::vector<float> input);
std::vector<float> IDCT_naive  (std::vector<float> input);
std::vector<float> IDXST_naive (std::vector<float> input);

std::vector<float> DCT_fft     (std::vector<float> input, bool normalize = false);
std::vector<float> IDCT_fft    (std::vector<float> input, bool normalize = false);
std::vector<float> IDXST_fft   (std::vector<float> input, bool normalize = false);

AIEPLACE_NAMESPACE_END