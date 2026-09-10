#include "DCT.h"
#include <complex>
#include <cmath>
#include <algorithm>
#include <map>
#include <mutex>

AIEPLACE_NAMESPACE_BEGIN

/// @brief Return the transpose of a rectangular 2D matrix: an R x C input gives a C x R output.
std::vector< std::vector<float> > transpose   (const std::vector< std::vector<float> >& input)
{
    if (input.empty()) return {};
    int num_rows = input.size();
    int num_cols = input[0].size();
    std::vector< std::vector<float> > output(num_cols, std::vector<float>(num_rows));

    // Parallel over input rows: each iteration writes one element of every output row, and
    // no two iterations touch the same element, so the result does not depend on the schedule.
    #pragma omp parallel for schedule(static)
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
 * @brief Precomputed constants for one transform length N: the bit-reversal permutation, the
 *        per-stage FFT root for each direction, and the two Makhoul twiddle sequences.
 *
 * These were previously recomputed on every call. Each DCT_fft/IDCT_fft evaluated one
 * std::polar (a libm sin + cos) per output element, so the 6N row transforms in a placement
 * iteration cost ~6N^2 transcendental calls — 12.6M per iteration at a 1024 grid, and the
 * dominant term inside dct_rowpass. The table stores exactly the values the old expressions
 * produced (same argument, same std::polar call), so the arithmetic downstream is unchanged
 * bit for bit; only the number of times sin/cos runs differs.
 */
struct DctTables
{
    std::vector<int> bit_reverse;                              // bit_reverse[i] = reverse(i)
    std::vector<std::complex<double>> root_fwd, root_inv;      // per FFT stage, indexed by log2(len)
    std::vector<std::complex<double>> twiddle_dct, twiddle_idct;
};

/**
 * @brief Tables for length @p N, built on first use and cached for the run.
 *
 * The lock is taken on every lookup, not just on the build: a returned reference must stay
 * valid while another thread inserts, and it is 6N lookups per iteration against ~10 us of
 * work per row, so the cost is unmeasurable. std::map is deliberate — its references are
 * stable across insertion, which an unordered_map rehash would not be.
 */
static const DctTables& dctTables(int N)
{
    static std::map<int, DctTables> cache;
    static std::mutex cache_mutex;
    std::lock_guard<std::mutex> lock(cache_mutex);

    auto found = cache.find(N);
    if (found != cache.end()) return found->second;

    DctTables& t = cache[N];

    // Same incremental bit-reversal the in-place loop used, materialized as a permutation.
    t.bit_reverse.assign(N, 0);
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        t.bit_reverse[i] = j;
    }

    int num_stages = 0;
    while ((1 << num_stages) < N) num_stages++;
    t.root_fwd.assign(num_stages + 1, 0.0);
    t.root_inv.assign(num_stages + 1, 0.0);
    for (int stage = 1; stage <= num_stages; stage++) {
        int len = 1 << stage;
        double ang_fwd = -1 * 2 * M_PI / len;
        double ang_inv = +1 * 2 * M_PI / len;
        t.root_fwd[stage] = std::complex<double>(cos(ang_fwd), sin(ang_fwd));
        t.root_inv[stage] = std::complex<double>(cos(ang_inv), sin(ang_inv));
    }

    t.twiddle_dct.resize(N);
    t.twiddle_idct.resize(N);
    for (int k = 0; k < N; k++) {
        t.twiddle_dct[k]  = std::polar(1.0, -M_PI * k / (2.0 * N));
        t.twiddle_idct[k] = std::polar(1.0,  M_PI * k / (2.0 * N));
    }
    return t;
}

/// @brief Per-thread scratch for the complex FFT working buffer, grown once and reused.
static std::vector<std::complex<double>>& fftScratch(int N)
{
    static thread_local std::vector<std::complex<double>> scratch;
    if ((int)scratch.size() < N) scratch.resize(N);
    return scratch;
}

/**
 * @brief Iterative in-place radix-2 Cooley-Tukey FFT (N a power of 2). @p roots holds the
 * per-stage root of unity for the wanted direction; neither direction is normalized
 * (callers scale as needed). */
static void fft(std::complex<double>* a, int n, const DctTables& t,
                const std::vector<std::complex<double>>& roots)
{
    for (int i = 1; i < n; i++)
        if (i < t.bit_reverse[i]) std::swap(a[i], a[t.bit_reverse[i]]);

    for (int stage = 1, len = 2; len <= n; stage++, len <<= 1) {
        std::complex<double> wlen = roots[stage];
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
void DCT_fft   (const float* in, float* out, int N, bool normalize)
{
    const DctTables& t = dctTables(N);
    std::complex<double>* v = fftScratch(N).data();
    for (int i = 0; i < N/2; i++) {   // even-odd reorder: v = [x0 x2 .. x_{N-1} .. x3 x1]
        v[i]       = in[2*i];
        v[N-1-i]   = in[2*i+1];
    }
    fft(v, N, t, t.root_fwd);
    double scale = normalize ? 1.0 / N : 1.0;
    for (int k = 0; k < N; k++)       // twiddle exp(-i*pi*k/2N)
        out[k] = (float)(scale * (t.twiddle_dct[k] * v[k]).real());
}

/** @brief DCT-III (inverse) via a single length-N FFT (Makhoul), matching IDCT_naive. */
void IDCT_fft  (const float* in, float* out, int N, bool normalize)
{
    const DctTables& t = dctTables(N);
    std::complex<double>* b = fftScratch(N).data();
    for (int n = 0; n < N; n++) {
        double cn = (n == 0) ? 0.5 : 1.0;            // c_0 = 1/2 (the .5*x_0 term)
        b[n] = cn * (double)in[n] * t.twiddle_idct[n];
    }
    fft(b, N, t, t.root_inv);
    double scale = normalize ? 1.0 / N : 1.0;
    for (int m = 0; m < N/2; m++) {                  // inverse of the DCT-II reorder
        out[2*m]     = (float)(scale * b[m].real());
        out[2*m+1]   = (float)(scale * b[N-1-m].real());
    }
}

/** @brief IDXST via IDCT_fft with the same input reversal + odd-output sign flip as IDXST_naive. */
void IDXST_fft (const float* in, float* out, int N, bool normalize)
{
    // The reversal must land in its own buffer: IDCT_fft may write @p out before this function
    // has finished reading @p in, and the two are allowed to alias.
    static thread_local std::vector<float> reversed;
    if ((int)reversed.size() < N) reversed.resize(N);

    reversed[0] = in[0];
    for (int n = 1; n < N; n++)
        reversed[n] = in[N-n];

    IDCT_fft(reversed.data(), out, N, normalize);
    for (int n = 1; n < N; n+=2)
        out[n] = -out[n];
}

AIEPLACE_NAMESPACE_END
