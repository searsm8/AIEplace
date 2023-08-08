#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN

vector< vector<float> > transpose   (vector< vector<float> > input);

vector<float> DCT_naive   (vector<float> input);
vector<float> IDCT_naive  (vector<float> input);
vector<float> IDXST_naive (vector<float> input);

vector<float> DCT_fft     (vector<float> input);
vector<float> IDCT_fft    (vector<float> input);
vector<float> IDXST_fft   (vector<float> input);

AIEPLACE_NAMESPACE_END