#include <vector>
#include <complex>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdint>
#include <cmath>
#include <cstring>

#include "xrt/xrt_device.h"
#include "experimental/xrt_xclbin.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_graph.h"
#include "xrt/xrt_bo.h"
#include "experimental/xrt_profile.h"


// Reads a text file with one complex number per line: "real imag"
bool read_complex_file(
    const std::string &filename,
    std::vector<std::complex<float>> &data,
    int expected_size = 1024
) {
    data.clear();

    std::ifstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Error: cannot open file " << filename << "\n";
        return false;
    }

    std::string line;
    int line_count = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        float re = 0.0f, im = 0.0f;
        if (!(iss >> re >> im)) {
            std::cerr << "Error: parse error in " << filename
                      << " at line " << (line_count + 1) << "\n";
            return false;
        }
        data.emplace_back(re, im);
        ++line_count;
    }

    if (expected_size > 0 && (int)data.size() != expected_size) {
        std::cerr << "Warning: file " << filename
                  << " has " << data.size()
                  << " complex samples, expected " << expected_size << "\n";
    }

    return true;
}

// Convenience wrappers for your specific files
bool load_fft_test_data(
    std::vector<std::complex<float>> &input,
    std::vector<std::complex<float>> &golden_output,
    const std::string &input_file  = "test_data/1lane/input.txt",
    const std::string &golden_file = "test_data/1lane/golden.txt",
    const unsigned int lanes = 1,
    const unsigned int size = 1024
) {
    if (!read_complex_file(input_file, input, lanes*size)) {
        std::cerr << "Failed to read input file\n";
        return false;
    }
    if (!read_complex_file(golden_file, golden_output, lanes*size)) {
        std::cerr << "Failed to read golden output file\n";
        return false;
    }
    return true;
}

uint16_t float32_to_float16(float f) {
    // Simple conversion using std::fpclassify assumptions; for testing only.
    // For production, use a robust fp32->fp16 routine or compiler built-ins.
    uint32_t x;
    std::memcpy(&x, &f, sizeof(f));

    uint32_t sign = (x >> 31) & 0x1;
    uint32_t exp  = (x >> 23) & 0xFF;
    uint32_t mant = x & 0x7FFFFF;

    uint16_t h = 0;

    if (exp == 0) {
        // Zero or subnormal -> just zero for simplicity
        h = (uint16_t)(sign << 15);
    } else if (exp == 0xFF) {
        // Inf/NaN -> map to Inf
        h = (uint16_t)((sign << 15) | (0x1F << 10));
    } else {
        int new_exp = (int)exp - 127 + 15;
        if (new_exp <= 0) {
            // Underflow -> zero
            h = (uint16_t)(sign << 15);
        } else if (new_exp >= 0x1F) {
            // Overflow -> Inf
            h = (uint16_t)((sign << 15) | (0x1F << 10));
        } else {
            uint16_t new_mant = (uint16_t)(mant >> 13); // truncate
            h = (uint16_t)((sign << 15) | (new_exp << 10) | new_mant);
        }
    }
    return h;
}

float float16_to_float32(uint16_t h) {
    uint32_t sign = (h >> 15) & 0x1;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;

    uint32_t x;

    if (exp == 0) {
        // Zero or subnormal -> just zero for simplicity
        x = sign << 31;
    } else if (exp == 0x1F) {
        // Inf/NaN -> Inf
        x = (sign << 31) | (0xFF << 23);
    } else {
        int new_exp = (int)exp - 15 + 127;
        uint32_t new_mant = mant << 13;
        x = (sign << 31) | (new_exp << 23) | new_mant;
    }

    float f;
    std::memcpy(&f, &x, sizeof(f));
    return f;
}

void pack_input_to_ddr64(
    const std::vector<std::complex<float>>& input,
    uint64_t* ddr_words,
    const int N
) {

    for (int i = 0; i < N / 2; ++i) {
        auto c0 = input[2 * i];
        auto c1 = input[2 * i + 1];

        uint16_t c0_real_h = float32_to_float16(c0.real());
        uint16_t c0_imag_h = float32_to_float16(c0.imag());
        uint16_t c1_real_h = float32_to_float16(c1.real());
        uint16_t c1_imag_h = float32_to_float16(c1.imag());

        uint64_t w = 0;
        w |= (uint64_t)c0_real_h;
        w |= (uint64_t)c0_imag_h << 16;
        w |= (uint64_t)c1_real_h << 32;
        w |= (uint64_t)c1_imag_h << 48;

        ddr_words[i] = w;
    }
}
void unpack_ddr64_to_output(
    const uint64_t* ddr_words,
    std::vector<std::complex<float>>& hw_output,
    const int N
) {

    for (int i = 0; i < N / 2; ++i) {
        uint64_t w = ddr_words[i];

        uint16_t c0_real_h = (uint16_t)( w        & 0xFFFF );
        uint16_t c0_imag_h = (uint16_t)((w >> 16) & 0xFFFF);
        uint16_t c1_real_h = (uint16_t)((w >> 32) & 0xFFFF);
        uint16_t c1_imag_h = (uint16_t)((w >> 48) & 0xFFFF);

        float c0_real = float16_to_float32(c0_real_h);
        float c0_imag = float16_to_float32(c0_imag_h);
        float c1_real = float16_to_float32(c1_real_h);
        float c1_imag = float16_to_float32(c1_imag_h);

        hw_output[2 * i]     = std::complex<float>(c0_real, c0_imag);
        hw_output[2 * i + 1] = std::complex<float>(c1_real, c1_imag);
    }
}

// Compare two complex<float> arrays with tolerance
bool compare_with_tolerance(
    const std::vector<std::complex<float>>& golden,
    const std::vector<std::complex<float>>& hw,
    float abs_tol = 1e-2f,   // adjust based on your FP16 error
    float rel_tol = 1e-2f
) {
    if (golden.size() != hw.size()) {
        std::cerr << "Size mismatch: golden=" << golden.size()
                  << " hw=" << hw.size() << "\n";
        return false;
    }

    bool ok = true;
    for (size_t k = 0; k < golden.size(); ++k) {
        float gr = golden[k].real();
        float gi = golden[k].imag();
        float hr = hw[k].real();
        float hi = hw[k].imag();

        float diff_r = std::fabs(gr - hr);
        float diff_i = std::fabs(gi - hi);

        float scale_r = std::fabs(gr) + 1e-7f;
        float scale_i = std::fabs(gi) + 1e-7f;

        bool bad_r = diff_r > abs_tol && diff_r / scale_r > rel_tol;
        bool bad_i = diff_i > abs_tol && diff_i / scale_i > rel_tol;

        if (bad_r || bad_i) {
            std::cout << "Mismatch at bin " << k
                      << " golden=(" << gr << "," << gi << ")"
                      << " hw=(" << hr << "," << hi << ")"
                      << " diff=(" << diff_r << "," << diff_i << ")\n";
            ok = false;
        }
    }
    return ok;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <bitstream_name> <test_data_input_filename> <test_data_golden_filename> <num_lanes>\n";
        return 1;
    }

    std::string bitstreamName = argv[1];
    std::string testInputFilename = argv[2];
    std::string testGoldenFilename = argv[3];
    unsigned int num_lanes = std::stoi(argv[4]);

    const unsigned int N = 1024;

    const unsigned int num_elements = N*num_lanes;
    std::vector<std::complex<float>> input;
    std::vector<std::complex<float>> golden;
    std::vector<std::complex<float>> hw_output;
    input.resize(num_elements);
    golden.resize(num_elements);
    hw_output.resize(num_elements);

    uint64_t in_ddr_words[num_elements] = {0};
    uint64_t out_ddr_words[num_elements] = {0};

    if (!load_fft_test_data(input, golden, testInputFilename, testGoldenFilename, num_lanes, N)) {
        return 1;
    }

    std::cout << "Loaded " << input.size()
              << " input samples and " << golden.size()
              << " golden FFT samples.\n";


    //pack_input_to_ddr64(input, in_ddr_words, N);
// xrt setup

    xrt::device device = xrt::device("c1:00.1");
    xrt::xclbin xclbin = xrt::xclbin(bitstreamName);
    xrt::uuid xclbin_uuid = device.load_xclbin(xclbin);

    xrt::kernel mm2s = xrt::kernel(device, xclbin_uuid, "density_mm2s");
    xrt::kernel s2mm = xrt::kernel(device, xclbin_uuid, "density_s2mm");
    xrt::bo input_bo = xrt::bo(device, num_elements*sizeof(uint64_t), xrt::bo::flags::device_only, mm2s.group_id(0));
    xrt::bo output_bo = xrt::bo(device, num_elements*sizeof(uint64_t), xrt::bo::flags::device_only, s2mm.group_id(0));
    xrt::run mm2s_run = xrt::run(mm2s);
    mm2s_run.set_arg(0, input_bo);
    mm2s_run.set_arg(1, N);
    xrt::run s2mm_run = xrt::run(s2mm);
    s2mm_run.set_arg(0, output_bo);
    s2mm_run.set_arg(1, N);

    input_bo.write(&input[0], num_elements*sizeof(uint64_t), 0);
    s2mm_run.start();
    mm2s_run.start();
    mm2s_run.wait();
    s2mm_run.wait();
    output_bo.read(&hw_output[0], num_elements*sizeof(uint64_t), 0);

    for (auto &z : hw_output) {
        float r = z.real();
        z.real(2.0f * r);   // real part times two
        z.imag(0.0f);       // imaginary part set to 0
    }

    //unpack_ddr64_to_output(out_ddr_words, hw_output, N);

    if (!compare_with_tolerance(golden, hw_output)) {
	    std::cout << "Hardware output doesn't match with expected golden!\n";
	    return 1;
    }
    std::cout << "Hardware output matches golden\n";


    return 0;
}
