#include <iostream>
#include <fstream>
#include <complex>
#include <vector>
#include <cmath>
#include <fftw3.h>

int main() {
    const int N = 1024;
    const int LANES = 8;
    const float TWO_PI = 6.283185307179586476925286766559f;

    // Time-domain input for 8 lanes: lanes[lane][sample]
    std::vector<std::vector<std::complex<float>>> lanes(
        LANES, std::vector<std::complex<float>>(N));

    // Generate test signal for each lane
    for (int lane = 0; lane < LANES; ++lane) {
        for (int n = 0; n < N; ++n) {
            float t = static_cast<float>(n) / N;

            float dc      = 1.0f;
            float cos5    = std::cos(TWO_PI * 5.0f * t);
            float sin20   = 0.5f * std::sin(TWO_PI * 20.0f * t);
            float cos100  = std::cos(TWO_PI * 100.0f * t);
            float sin100  = std::sin(TWO_PI * 100.0f * t);

            float real_part = dc + cos5 + sin20 + cos100;
            float imag_part = sin100;

            lanes[lane][n] = std::complex<float>(real_part, imag_part);
        }
    }

    // Prepare FFTW buffers for one lane
    fftwf_complex* in  = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);
    fftwf_complex* out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);

    if (!in || !out) {
        std::cerr << "Error: FFTW malloc failed\n";
        return 1;
    }

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

    std::ofstream fin_interleaved("input_interleaved.txt");
    std::ofstream fout_interleaved("golden_interleaved.txt");
    fin_interleaved << std::fixed;
    fout_interleaved << std::fixed;

    // Optional per-lane files
    std::vector<std::ofstream> fin_lane(LANES);
    std::vector<std::ofstream> fout_lane(LANES);
    for (int lane = 0; lane < LANES; ++lane) {
        fin_lane[lane].open("input_lane" + std::to_string(lane) + ".txt");
        fout_lane[lane].open("golden_lane" + std::to_string(lane) + ".txt");
        fin_lane[lane]  << std::fixed;
        fout_lane[lane] << std::fixed;
    }

    // Write interleaved input: x[0] lane0..7, x[1] lane0..7, ...
    for (int n = 0; n < N; ++n) {
        for (int lane = 0; lane < LANES; ++lane) {
            const auto& c = lanes[lane][n];
            fin_interleaved << c.real() << " " << c.imag() << "\n";
        }
    }

    // Buffer to store FFT results for all lanes: fft_results[lane][bin]
    std::vector<std::vector<std::complex<float>>> fft_results(
        LANES, std::vector<std::complex<float>>(N));

    // Per-lane FFT
    for (int lane = 0; lane < LANES; ++lane) {
        // Copy data to FFTW input buffer
        for (int n = 0; n < N; ++n) {
            const std::complex<float>& c = lanes[lane][n];
            in[n][0] = c.real();
            in[n][1] = c.imag();
            fin_lane[lane] << c.real() << " " << c.imag() << "\n";
        }

        // Execute FFT
        fftwf_execute(plan);

        // Store FFT output and write per-lane golden
        for (int k = 0; k < N; ++k) {
            float re = out[k][0];
            float im = out[k][1];
            fft_results[lane][k] = std::complex<float>(re, im);
            fout_lane[lane] << re << " " << im << "\n";
        }
    }

    // Write interleaved golden FFT output: X[0] lane0..7, X[1] lane0..7, ...
    for (int k = 0; k < N; ++k) {
        for (int lane = 0; lane < LANES; ++lane) {
            const auto& c = fft_results[lane][k];
            fout_interleaved << c.real() << " " << c.imag() << "\n";
        }
    }

    // Simple sanity check: print magnitudes for lane 0
    std::cout << "FFT result (lane 0, magnitude) for a few key bins:\n";
    auto mag = [&](int k) {
        const auto& c = fft_results[0][k];
        return std::sqrt(c.real()*c.real() + c.imag()*c.imag());
    };

    std::cout << "Bin 0   (DC)   magnitude: " << mag(0)   << "\n";
    std::cout << "Bin 5           magnitude: " << mag(5)   << "\n";
    std::cout << "Bin 20          magnitude: " << mag(20)  << "\n";
    std::cout << "Bin 100         magnitude: " << mag(100) << "\n";

    // Cleanup
    fftwf_destroy_plan(plan);
    fftwf_free(in);
    fftwf_free(out);

    fin_interleaved.close();
    fout_interleaved.close();
    for (int lane = 0; lane < LANES; ++lane) {
        fin_lane[lane].close();
        fout_lane[lane].close();
    }

    return 0;
}
