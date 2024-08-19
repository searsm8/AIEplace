#include "DCT.h"

AIEPLACE_NAMESPACE_BEGIN

std::vector< std::vector<float> > transpose   (std::vector< std::vector<float> > input)
{
    int num_rows = input.size();
    int num_cols = input[0].size();
    std::vector< std::vector<float> > output(num_rows, std::vector<float>(num_cols));

    for (int row_index = 0; row_index < num_rows; row_index++)
    {
        for (int col_index = 0; col_index < num_cols; col_index++)
        {
            output[row_index][col_index] = input[col_index][row_index];
        }
    }
    return output;
}

/* @brief: perform Discrete Cosine transform naively using the definition
 * DCT(x_n)_k = SUM ( x_n * cos(PI/N * (n+.5) * k))
 */
std::vector<float> DCT_naive   (std::vector<float> input)
{
    int N = input.size();
    std::vector<float> result(N);
    for (int k = 0; k < N; k++)
    {
        double sum = 0;
        for (int n = 0; n < N; n++)
        {
           sum += input[n] * cos(M_PI/N * (n + .5) * k);
        }
        //if(k == 0) sum *= 1 / sqrt(N); // scale factor
        //else sum *= sqrt(2.0/N);
        result[k] = sum;
    }
    return result;
}

/* @brief: perform Inverse Discrete Cosine transform naively using the definition
 * IDCT(x_n)_k = .5*x_0 + SUM ( x_n * cos(PI/N * (k+.5) * n))
 */
std::vector<float> IDCT_naive  (std::vector<float> input)
{
    int N = input.size();
    std::vector<float> result(N);
    for (int k = 0; k < N; k++)
    {
        float sum = 0;
        for (int n = 1; n < N; n++)
        {
           sum += input[n] * cos(M_PI/N * (k + .5) * n);
        }
        result[k] = 0.5*input[0] + sum;
    }
    return result;
}

/* @brief: perform Inverse Discrete Cosine transform naively using the definition
 * IDXST(x_n)_k = (-1)^k * IDCT({x_(N-n)})_k
 */
std::vector<float> IDXST_naive (std::vector<float> input)
{
    int N = input.size();
    std::vector<float> temp(N);

    // reorder input to be x_(N-n)
    // x[0] remains unchanged
    temp[0] = input[0];
    for (int n = 1; n < N; n++)
        temp[n] = input[N-n];

    // mult by (-1)^k

    temp = IDCT_naive(temp);
    for (int n = 1; n < N; n+=2)
        temp[n] *= -1;
    
    return temp;
}

/* @brief: perform Discrete Cosine transform using fft as a subroutine
*/
std::vector<float> DCT_fft   (std::vector<float> input)
{
  std::vector<float> result(input.size());

  return result;

}

std::vector<float> IDCT_fft  (std::vector<float> input)
{
  std::vector<float> result(input.size());

  return result;

}

std::vector<float> IDXST_fft (std::vector<float> input)
{
  std::vector<float> result(input.size());

  return result;

}

AIEPLACE_NAMESPACE_END