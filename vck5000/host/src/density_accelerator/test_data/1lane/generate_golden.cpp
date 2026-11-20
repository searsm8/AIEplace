#include <iostream>
#include <fstream>
#include <complex>
#include <vector>
#include <cmath>
#include <fftw3.h>

int main() {
    const int N = 1024;
    const float TWO_PI = 6.283185307179586476925286766559f;

    // Time-domain input: 1024 complex samples (cfloat-equivalent)
    std::vector<std::complex<float>> x(N);

    // Generate test signal
    for (int n = 0; n < N; ++n) {
        float t = static_cast<float>(n) / N;

        // Components:
        // DC
        float dc = 1.0f;

        // Cosine at bin 5
        float cos5 = std::cos(TWO_PI * 5.0f * t);

        // Sine at bin 20
        float sin20 = 0.5f * std::sin(TWO_PI * 20.0f * t);

        // Complex exponential at bin 100
        float cos100 = std::cos(TWO_PI * 100.0f * t);
        float sin100 = std::sin(TWO_PI * 100.0f * t);

        float real_part = dc + cos5 + sin20 + cos100;
        float imag_part = sin100;

        x[n] = std::complex<float>(real_part, imag_part);
    }

    // Prepare FFTW input/output buffers (single precision)
    fftwf_complex* in  = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);
    fftwf_complex* out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);

    if (!in || !out) {
        std::cerr << "Error: FFTW malloc failed\n";
        return 1;
    }

    // Copy data to FFTW input buffer
    for (int n = 0; n < N; ++n) {
        in[n][0] = x[n].real(); // real
        in[n][1] = x[n].imag(); // imag
    }

    // Create FFT plan: forward, in-place or out-of-place
    fftwf_plan plan = fftwf_plan_dft_1d(
        N,
        in,
        out,
        FFTW_FORWARD,
        FFTW_ESTIMATE
    );

    if (!plan) {
        std::cerr << "Error: FFTW plan creation failed\n";
        fftwf_free(in);
        fftwf_free(out);
        return 1;
    }

    // Execute FFT
    fftwf_execute(plan);

    // Cleanup plan
    fftwf_destroy_plan(plan);

    // Optionally, write input and output to files for exact comparison
    {
        std::ofstream fin("input_1024_cfloat.txt");
        std::ofstream fout("fft_output_1024_cfloat.txt");

        fin << std::fixed;
        fout << std::fixed;

        for (int n = 0; n < N; ++n) {
            fin << x[n].real() << " " << x[n].imag() << "\n";
        }

        for (int k = 0; k < N; ++k) {
            float re = out[k][0];
            float im = out[k][1];
            fout << re << " " << im << "\n";
        }

        fin.close();
        fout.close();
    }

    // Also print a few bins to stdout for quick sanity check
    std::cout << "FFT result (magnitude) for a few key bins:\n";
    auto mag = [&](int k) {
        float re = out[k][0];
        float im = out[k][1];
        return std::sqrt(re*re + im*im);
    };

    std::cout << "Bin 0   (DC)   magnitude: " << mag(0)   << "\n";
    std::cout << "Bin 5           magnitude: " << mag(5)   << "\n";
    std::cout << "Bin 20          magnitude: " << mag(20)  << "\n";
    std::cout << "Bin 100         magnitude: " << mag(100) << "\n";

    // Free buffers
    fftwf_free(in);
    fftwf_free(out);

    return 0;
}
