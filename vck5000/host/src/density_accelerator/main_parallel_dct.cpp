#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <iomanip>

#include "xrt/xrt_device.h"
#include "experimental/xrt_xclbin.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_graph.h"
#include "xrt/xrt_bo.h"
#include "experimental/xrt_profile.h"


// Reads a text file with one real number per line
bool read_real_file(
    const std::string &filename,
    std::vector<float> &data,
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
        float re = 0.0f;
        if (!(iss >> re)) {
            std::cerr << "Error: parse error in " << filename
                      << " at line " << (line_count + 1) << "\n";
            return false;
        }
        data.emplace_back(re);
        ++line_count;
    }

    if (expected_size > 0 && (int)data.size() != expected_size) {
        std::cerr << "Warning: file " << filename
                  << " has " << data.size()
                  << " real samples, expected " << expected_size << "\n";
    }

    return true;
}

// Convenience wrappers for your specific files
bool load_fft_test_data(
    std::vector<float> &input,
    std::vector<float> &golden_output,
    const std::string &input_file  = "test_data/1lane/input.txt",
    const std::string &golden_file = "test_data/1lane/golden.txt",
    const unsigned int lanes = 1,
    const unsigned int size = 1024
) {
    if (!read_real_file(input_file, input, lanes*size)) {
        std::cerr << "Failed to read input file\n";
        return false;
    }
    if (!read_real_file(golden_file, golden_output, lanes*size)) {
        std::cerr << "Failed to read golden output file\n";
        return false;
    }
    return true;
}

// Compare two float arrays with tolerance
bool compare_with_tolerance(
    const std::vector<float>& golden,
    const std::vector<float>& hw,
    float abs_tol = 1e-2f,   // adjust based on your FP16 error
    float rel_tol = 1e-2f
) {
    if (golden.size() != hw.size()) {
        std::cerr << "Size mismatch: golden=" << golden.size()
                  << " hw=" << hw.size() << "\n";
        return false;
    }

    bool ok = true;
    int num_errors = 0;
    for (size_t k = 0; k < golden.size(); ++k) {
	//if (num_errors > 32) {
	//	return ok;
	//}
        float gr = golden.at(k);
        float hr = hw.at(k);

        float diff_r = std::fabs(gr - hr);

        float scale_r = std::fabs(gr) + 1e-7f;

        bool bad_r = diff_r > abs_tol && diff_r / scale_r > rel_tol;

        if (bad_r) {
	    num_errors++;
            std::cout << "Mismatch at bin " << k
                      << " golden=(" << gr << ")"
                      << " hw=(" << hr << ")"
                      << " diff=(" << diff_r << ")\n";
            ok = false;
        }
    }
    return ok;
}

bool debug_compare_with_tolerance_table(
    const std::vector<float>& golden,
    const std::vector<float>& hw,
    int lanes,
    bool show_all_bins,
    float abs_tol = 1e-2f,
    float rel_tol = 1e-2f
) {
    if (lanes <= 0) {
        std::cerr << "Invalid number of lanes: " << lanes << '\n';
        return false;
    }

    if (hw.size() % static_cast<std::size_t>(lanes) != 0) {
        std::cerr << "HW size " << hw.size()
                  << " is not divisible by the number of lanes "
                  << lanes << '\n';
        return false;
    }

    const std::size_t num_bins =
        hw.size() / static_cast<std::size_t>(lanes);

    // Supported golden layouts:
    //   1. One expected value per bin: golden.size() == num_bins
    //   2. Expected value repeated per lane: golden.size() == hw.size()
    const bool golden_per_bin = golden.size() == num_bins;
    const bool golden_per_lane = golden.size() == hw.size();

    if (!golden_per_bin && !golden_per_lane) {
        std::cerr
            << "Golden size mismatch. Expected either " << num_bins
            << " values (one per bin) or " << hw.size()
            << " values (one per lane), but got "
            << golden.size() << '\n';
        return false;
    }

    auto values_match = [abs_tol, rel_tol](float expected, float actual) {
        // Handles equal finite values and equal infinities.
        if (expected == actual) {
            return true;
        }

        // NaN, or infinities with different signs, are mismatches.
        if (!std::isfinite(expected) || !std::isfinite(actual)) {
            return false;
        }

        const float diff = std::fabs(expected - actual);
        const float scale = std::fabs(expected) + 1e-7f;

        return !(diff > abs_tol && diff / scale > rel_tol);
    };

    auto format_float = [](float value) {
        std::ostringstream stream;
        stream << std::scientific << std::setprecision(6) << value;
        return stream.str();
    };

    constexpr int bin_width = 10;
    constexpr int result_width = 8;
    constexpr int value_width = 15;

    auto print_cell = [](const std::string& value, int width) {
        std::cout << ' ' << std::left << std::setw(width) << value << '|';
    };

    auto print_separator = [&](int width) {
        std::cout << '+' << std::string(width + 1, '-');
    };

    // Print table header.
    print_cell("bin", bin_width);
    print_cell("Result", result_width);
    print_cell("Expected", value_width);

    for (int lane = 0; lane < lanes; ++lane) {
        print_cell("lane " + std::to_string(lane), value_width);
    }
    std::cout << '\n';

    print_separator(bin_width);
    print_separator(result_width);
    print_separator(value_width);

    for (int lane = 0; lane < lanes; ++lane) {
        print_separator(value_width);
    }
    std::cout << "+\n";

    bool all_ok = true;
    std::size_t bad_bins = 0;
    std::size_t bad_values = 0;

    for (std::size_t bin = 0; bin < num_bins; ++bin) {
        const std::size_t base =
            bin * static_cast<std::size_t>(lanes);

        // All lanes in a bin are expected to have the same answer.
        const float expected =
            golden_per_bin ? golden[bin] : golden[base];

        bool bin_ok = true;

        for (int lane = 0; lane < lanes; ++lane) {
            const std::size_t index =
                base + static_cast<std::size_t>(lane);

            if (!values_match(expected, hw[index])) {
                bin_ok = false;
                ++bad_values;
            }
        }

        if (!bin_ok) {
            all_ok = false;
            ++bad_bins;
        }

        if (!show_all_bins && bin_ok) {
            continue;
        }

        // Use bin + 1 for one-based bin numbering.
        print_cell("bin " + std::to_string(bin + 1), bin_width);
        print_cell(bin_ok ? "OK" : "BAD", result_width);
        print_cell(format_float(expected), value_width);

        for (int lane = 0; lane < lanes; ++lane) {
            const std::size_t index =
                base + static_cast<std::size_t>(lane);

            print_cell(format_float(hw[index]), value_width);
        }

        std::cout << '\n';
    }

    std::cout << "\nSummary: "
              << bad_bins << " bad bin(s), "
              << bad_values << " bad lane value(s), "
              << num_bins << " total bin(s).\n";

    return all_ok;
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
    std::vector<float> input;
    std::vector<float> golden;
    std::vector<float> hw_output_0;
    std::vector<float> hw_output_1;
    std::vector<float> hw_output_2;
    std::vector<float> hw_output_3;
    input.resize(num_elements);
    golden.resize(num_elements);
    hw_output_0.resize(num_elements);
    hw_output_1.resize(num_elements);
    hw_output_2.resize(num_elements);
    hw_output_3.resize(num_elements);

    float in_ddr_words[num_elements] = {0};
    float out_ddr_words[num_elements] = {0};

    if (!load_fft_test_data(input, golden, testInputFilename, testGoldenFilename, num_lanes, N)) {
        return 1;
    }

    std::cout << "Loaded " << input.size()
              << " input samples and " << golden.size()
              << " golden FFT samples.\n";


    // xrt setup
    xrt::device device = xrt::device("c1:00.1");
    xrt::xclbin xclbin = xrt::xclbin(bitstreamName);
    xrt::uuid xclbin_uuid = device.load_xclbin(xclbin);


    // Get IP layout section
    auto kernels = xclbin.get_kernels();
    for (auto kernel : kernels) {
	    std::cout << kernel.get_name() << "  ";

	    for (const auto& cu : kernel.get_cus()) {
		    std::cout << "  CU: " << cu.get_name() << '\n';
	    }
    }


    xrt::kernel mm2s_0 = xrt::kernel(device, xclbin_uuid, "density_mm2s:{density_mm2s_0}");
    xrt::kernel s2mm_0 = xrt::kernel(device, xclbin_uuid, "density_s2mm:{density_s2mm_0}");
    xrt::kernel mm2s_1 = xrt::kernel(device, xclbin_uuid, "density_mm2s:{density_mm2s_1}");
    xrt::kernel s2mm_1 = xrt::kernel(device, xclbin_uuid, "density_s2mm:{density_s2mm_1}");
    xrt::kernel mm2s_2 = xrt::kernel(device, xclbin_uuid, "density_mm2s:{density_mm2s_2}");
    xrt::kernel s2mm_2 = xrt::kernel(device, xclbin_uuid, "density_s2mm:{density_s2mm_2}");
    xrt::kernel mm2s_3 = xrt::kernel(device, xclbin_uuid, "density_mm2s:{density_mm2s_3}");
    xrt::kernel s2mm_3 = xrt::kernel(device, xclbin_uuid, "density_s2mm:{density_s2mm_3}");
    xrt::bo input_bo_0  = xrt::bo(device, num_elements*sizeof(float), xrt::bo::flags::device_only, mm2s_0.group_id(0));
    xrt::bo output_bo_0 = xrt::bo(device, num_elements*sizeof(float), xrt::bo::flags::device_only, s2mm_0.group_id(0));
    xrt::bo input_bo_1  = xrt::bo(device, num_elements*sizeof(float), xrt::bo::flags::device_only, mm2s_1.group_id(0));
    xrt::bo output_bo_1 = xrt::bo(device, num_elements*sizeof(float), xrt::bo::flags::device_only, s2mm_1.group_id(0));
    xrt::bo input_bo_2  = xrt::bo(device, num_elements*sizeof(float), xrt::bo::flags::device_only, mm2s_2.group_id(0));
    xrt::bo output_bo_2 = xrt::bo(device, num_elements*sizeof(float), xrt::bo::flags::device_only, s2mm_2.group_id(0));
    xrt::bo input_bo_3  = xrt::bo(device, num_elements*sizeof(float), xrt::bo::flags::device_only, mm2s_3.group_id(0));
    xrt::bo output_bo_3 = xrt::bo(device, num_elements*sizeof(float), xrt::bo::flags::device_only, s2mm_3.group_id(0));
    xrt::run mm2s_0_run = xrt::run(mm2s_0);
    mm2s_0_run.set_arg(0, input_bo_0);
    mm2s_0_run.set_arg(1, N);
    xrt::run s2mm_0_run = xrt::run(s2mm_0);
    s2mm_0_run.set_arg(0, output_bo_0);
    s2mm_0_run.set_arg(1, N);
    xrt::run mm2s_1_run = xrt::run(mm2s_1);
    mm2s_1_run.set_arg(0, input_bo_1);
    mm2s_1_run.set_arg(1, N);
    xrt::run s2mm_1_run = xrt::run(s2mm_1);
    s2mm_1_run.set_arg(0, output_bo_1);
    s2mm_1_run.set_arg(1, N);
    xrt::run mm2s_2_run = xrt::run(mm2s_2);
    mm2s_2_run.set_arg(0, input_bo_2);
    mm2s_2_run.set_arg(1, N);
    xrt::run s2mm_2_run = xrt::run(s2mm_2);
    s2mm_2_run.set_arg(0, output_bo_2);
    s2mm_2_run.set_arg(1, N);
    xrt::run mm2s_3_run = xrt::run(mm2s_3);
    mm2s_3_run.set_arg(0, input_bo_3);
    mm2s_3_run.set_arg(1, N);
    xrt::run s2mm_3_run = xrt::run(s2mm_3);
    s2mm_3_run.set_arg(0, output_bo_3);
    s2mm_3_run.set_arg(1, N);

    // xrt run
    input_bo_0.write(&input[0], num_elements*sizeof(float), 0);
    input_bo_1.write(&input[0], num_elements*sizeof(float), 0);
    input_bo_2.write(&input[0], num_elements*sizeof(float), 0);
    input_bo_3.write(&input[0], num_elements*sizeof(float), 0);
    s2mm_0_run.start();
    mm2s_0_run.start();
    s2mm_1_run.start();
    mm2s_1_run.start();
    s2mm_2_run.start();
    mm2s_2_run.start();
    s2mm_3_run.start();
    mm2s_3_run.start();

    mm2s_0_run.wait();
    s2mm_0_run.wait();
    mm2s_1_run.wait();
    s2mm_1_run.wait();
    mm2s_2_run.wait();
    s2mm_2_run.wait();
    mm2s_3_run.wait();
    s2mm_3_run.wait();

    output_bo_0.read(&hw_output_0[0], num_elements*sizeof(float), 0);
    output_bo_1.read(&hw_output_1[0], num_elements*sizeof(float), 0);
    output_bo_2.read(&hw_output_2[0], num_elements*sizeof(float), 0);
    output_bo_3.read(&hw_output_3[0], num_elements*sizeof(float), 0);

    // compare fpga vs cpu
    if (!debug_compare_with_tolerance_table(golden, hw_output_0, num_lanes, true)) {
	    std::cout << "Hardware output doesn't match with expected golden!\n";
	    return 1;
    }
    std::cout << "Hardware output matches golden\n";


    return 0;
}
