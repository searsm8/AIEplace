#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN

// TODO: These functions should be operating on the parameter passed by reference, not copying large data structures!
std::vector< std::vector<float> > transpose   (std::vector< std::vector<float> > input);

std::vector<float> DCT_naive   (std::vector<float> input);
std::vector<float> IDCT_naive  (std::vector<float> input);
std::vector<float> IDXST_naive (std::vector<float> input);

std::vector<float> DCT_fft     (std::vector<float> input);
std::vector<float> IDCT_fft    (std::vector<float> input);
std::vector<float> IDXST_fft   (std::vector<float> input);

AIEPLACE_NAMESPACE_END