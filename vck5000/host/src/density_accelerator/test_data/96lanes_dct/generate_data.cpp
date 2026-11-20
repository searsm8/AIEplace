#include <iostream>
#include <fstream>
#include <complex>
#include <vector>
#include <cmath>
#include <fftw3.h>

int main() {
    const int N = 1024;
    const int LANES = 96;
    const float TWO_PI = 6.283185307179586476925286766559f;

    // Time-domain input for 16 lanes: lanes[lane][sample]
    std::vector<std::vector<float>> lanes(
        LANES, std::vector<float>(N));

    // Generate test signal for each lane (complex)
    for (int lane = 0; lane < LANES; ++lane) {
        for (int n = 0; n < N; ++n) {
            float t = static_cast<float>(n) / N;

            float dc      = 0.5f;
            float cos5    = 0.5f * std::cos(TWO_PI * 5.0f * t);
            float sin20   = 0.5f * std::sin(TWO_PI * 20.0f * t);
            float cos100  = std::cos(TWO_PI * 100.0f * t);
            float sin100  = 0.5f * std::sin(TWO_PI * 100.0f * t);

            float real_part = dc + cos5 + sin100;
            //float real_part = dc + cos5 + sin20 + cos100;
            //float imag_part = sin100;

            lanes[lane][n] = real_part;
        }
    }

    // FFTW real-to-real (DCT-II) buffers for one lane
    float* in  = (float*)fftwf_malloc(sizeof(float) * N);
    float* out = (float*)fftwf_malloc(sizeof(float) * N);

    if (!in || !out) {
        std::cerr << "Error: FFTW malloc failed\n";
        return 1;
    }

    // DCT-II plan: REDFT10
    fftwf_plan plan = fftwf_plan_r2r_1d(
        N,
        in,
        out,
        FFTW_REDFT10,    // DCT-II
        FFTW_ESTIMATE
    );

    if (!plan) {
        std::cerr << "Error: FFTW DCT plan creation failed\n";
        fftwf_free(in);
        fftwf_free(out);
        return 1;
    }

    // Files
    std::ofstream fin_interleaved("input_interleaved.txt");
    std::ofstream fout_interleaved("golden_interleaved.txt");
    fin_interleaved  << std::fixed;
    fout_interleaved << std::fixed;

    std::vector<std::ofstream> fin_lane(LANES);
    std::vector<std::ofstream> fout_lane(LANES);
    for (int lane = 0; lane < LANES; ++lane) {
        fin_lane[lane].open("input_lane"  + std::to_string(lane) + ".txt");
        fout_lane[lane].open("golden_lane" + std::to_string(lane) + ".txt");
        fin_lane[lane]  << std::fixed;
        fout_lane[lane] << std::fixed;
    }

    // Write interleaved complex input: x[0] lane0..7, x[1] lane0..7, ...
    for (int n = 0; n < N; ++n) {
        for (int lane = 0; lane < LANES; ++lane) {
            fin_interleaved << lanes[lane][n] << "\n";
        }
    }

    // Buffer to store complex DCT results for all lanes:
    // dct_results[lane][k].real() = DCT coeff, imag() = 0
    std::vector<std::vector<float>> dct_results(
        LANES, std::vector<float>(N));

    // Per-lane DCT: real(in) = real part of complex input; output made complex with imag=0
    for (int lane = 0; lane < LANES; ++lane) {
        // Fill lane input files & DCT input (real part only)
        for (int n = 0; n < N; ++n) {
            const float& real = lanes[lane][n];

            // Keep full complex input in per-lane input file
            fin_lane[lane] << real << "\n";

            // DCT input: real part only
            in[n] = real;
        }

        // Execute DCT-II
        fftwf_execute(plan);

        // Store as complex and write per-lane golden (real, 0)
        for (int k = 0; k < N; ++k) {
            float dct_val = out[k];
            dct_results[lane][k] = dct_val;
            fout_lane[lane] << dct_val << "\n";
        }
    }

    // Write interleaved complex golden DCT output:
    // X[0] lane0..7, X[1] lane0..7, ...
    for (int k = 0; k < N; ++k) {
        for (int lane = 0; lane < LANES; ++lane) {
            const auto& real = dct_results[lane][k];
            fout_interleaved << real << "\n";
        }
    }

    // Simple sanity check: lane 0 magnitudes (imag is always 0)
    std::cout << "DCT-II result (lane 0) for a few key bins (magnitude == abs(real)):\n";
    std::cout << "Bin 0   magnitude: " << dct_results[0][0]   << "\n";
    std::cout << "Bin 5   magnitude: " << dct_results[0][5]   << "\n";
    std::cout << "Bin 20  magnitude: " << dct_results[0][20]  << "\n";
    std::cout << "Bin 100 magnitude: " << dct_results[0][100] << "\n";

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
