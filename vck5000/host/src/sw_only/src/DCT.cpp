#include "DCT.h"
#include <complex>
#include <cmath>
#include <algorithm>

AIEPLACE_NAMESPACE_BEGIN

/// @brief Return the transpose of a rectangular 2D matrix: an R x C input gives a C x R output.
std::vector< std::vector<float> > transpose   (const std::vector< std::vector<float> >& input)
{
    if (input.empty()) return {};
    int num_rows = input.size();
    int num_cols = input[0].size();
    std::vector< std::vector<float> > output(num_cols, std::vector<float>(num_rows));

    for (int row_index = 0; row_index < num_rows; row_index++)
    {
        for (int col_index = 0; col_index < num_cols; col_index++)
        {
            output[col_index][row_index] = input[row_index][col_index];
        }
    }
    return output;
}

/** @brief: perform Discrete Cosine transform naively using the definition
 * DCT(x_n)_k = SUM ( x_n * cos(PI/N * (n+.5) * k))
 */
std::vector<float> DCT_naive   (const std::vector<float>& input)
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
        result[k] = sum;
    }
    return result;
}

/** @brief: perform Inverse Discrete Cosine transform naively using the definition
 * IDCT(x_n)_k = .5*x_0 + SUM ( x_n * cos(PI/N * (k+.5) * n))
 */
std::vector<float> IDCT_naive  (const std::vector<float>& input)
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

/** @brief: perform Inverse Discrete Cosine transform naively using the definition
 * IDXST(x_n)_k = (-1)^k * IDCT({x_(N-n)})_k
 */
std::vector<float> IDXST_naive (const std::vector<float>& input)
{
    int N = input.size();
    std::vector<float> temp(N);

    temp[0] = input[0];
    for (int n = 1; n < N; n++)
        temp[n] = input[N-n];

    temp = IDCT_naive(temp);
    for (int n = 1; n < N; n+=2)
        temp[n] *= -1;

    return temp;
}

/**
 * @brief Iterative in-place radix-2 Cooley-Tukey FFT (N a power of 2). sign=-1 forward,
 * sign=+1 inverse; neither direction is normalized (callers scale as needed). */
static void fft(std::vector<std::complex<double>>& a, int sign)
{
    int n = a.size();
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = sign * 2 * M_PI / len;
        std::complex<double> wlen(cos(ang), sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (int k = 0; k < len/2; k++) {
                std::complex<double> u = a[i+k];
                std::complex<double> v = a[i+k+len/2] * w;
                a[i+k]         = u + v;
                a[i+k+len/2]   = u - v;
                w *= wlen;
            }
        }
    }
}

/** @brief DCT-II via a single length-N FFT (Makhoul). Equivalent to DCT_naive to
 * float precision but O(N log N). normalize=true applies the 1/N scale factor that
 * keeps intermediate magnitudes bounded (a global constant, absorbed by lambda). */
std::vector<float> DCT_fft   (const std::vector<float>& input, bool normalize)
{
    int N = input.size();
    std::vector<std::complex<double>> v(N);
    for (int i = 0; i < N/2; i++) {   // even-odd reorder: v = [x0 x2 .. x_{N-1} .. x3 x1]
        v[i]       = input[2*i];
        v[N-1-i]   = input[2*i+1];
    }
    fft(v, -1);
    double scale = normalize ? 1.0 / N : 1.0;
    std::vector<float> out(N);
    for (int k = 0; k < N; k++) {
        double ang = -M_PI * k / (2.0 * N);          // twiddle exp(-i*pi*k/2N)
        out[k] = (float)(scale * (std::polar(1.0, ang) * v[k]).real());
    }
    return out;
}

/** @brief DCT-III (inverse) via a single length-N FFT (Makhoul), matching IDCT_naive. */
std::vector<float> IDCT_fft  (const std::vector<float>& input, bool normalize)
{
    int N = input.size();
    std::vector<std::complex<double>> b(N);
    for (int n = 0; n < N; n++) {
        double cn  = (n == 0) ? 0.5 : 1.0;           // c_0 = 1/2 (the .5*x_0 term)
        double ang = M_PI * n / (2.0 * N);
        b[n] = cn * (double)input[n] * std::polar(1.0, ang);
    }
    fft(b, +1);
    double scale = normalize ? 1.0 / N : 1.0;
    std::vector<float> x(N);
    for (int m = 0; m < N/2; m++) {                  // inverse of the DCT-II reorder
        x[2*m]     = (float)(scale * b[m].real());
        x[2*m+1]   = (float)(scale * b[N-1-m].real());
    }
    return x;
}

/** @brief IDXST via IDCT_fft with the same input reversal + odd-output sign flip as IDXST_naive. */
std::vector<float> IDXST_fft (const std::vector<float>& input, bool normalize)
{
    int N = input.size();
    std::vector<float> temp(N);
    temp[0] = input[0];
    for (int n = 1; n < N; n++)
        temp[n] = input[N-n];

    temp = IDCT_fft(temp, normalize);
    for (int n = 1; n < N; n+=2)
        temp[n] *= -1;

    return temp;
}

AIEPLACE_NAMESPACE_END
