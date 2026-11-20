#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "experimental/xrt_xclbin.h"

namespace {

constexpr std::size_t MATRIX_SIZE = 1024;
constexpr std::size_t NUM_ELEMENTS = MATRIX_SIZE * MATRIX_SIZE;
constexpr std::size_t NUM_BYTES = NUM_ELEMENTS * sizeof(float);

bool read_float_matrix(
    const std::string& filename,
    std::vector<float>& data)
{
    std::ifstream file(filename);

    if (!file) {
        std::cerr << "Error: cannot open " << filename << '\n';
        return false;
    }

    data.clear();
    data.reserve(NUM_ELEMENTS);

    float value = 0.0f;

    // This accepts both space-separated matrices and one value per line.
    while (file >> value) {
        data.push_back(value);

        if (data.size() > NUM_ELEMENTS) {
            std::cerr
                << "Error: " << filename
                << " contains more than " << NUM_ELEMENTS
                << " values\n";
            return false;
        }
    }

    if (!file.eof()) {
        std::cerr
            << "Error: invalid floating-point value in "
            << filename << '\n';
        return false;
    }

    if (data.size() != NUM_ELEMENTS) {
        std::cerr
            << "Error: " << filename
            << " contains " << data.size()
            << " values; expected exactly "
            << NUM_ELEMENTS << '\n';
        return false;
    }

    return true;
}

struct ComparisonResult {
    bool passed = true;
    std::size_t mismatch_count = 0;
    std::size_t non_finite_count = 0;

    float maximum_absolute_error = 0.0f;
    float maximum_relative_error = 0.0f;

    std::size_t maximum_absolute_error_index = 0;
    std::size_t maximum_relative_error_index = 0;
};

ComparisonResult compare_matrices(
    const std::vector<float>& golden,
    const std::vector<float>& hardware,
    float absolute_tolerance,
    float relative_tolerance,
    std::size_t maximum_mismatches_to_print)
{
    ComparisonResult result;

    if (golden.size() != hardware.size()) {
        std::cerr
            << "Error: size mismatch: golden="
            << golden.size()
            << ", hardware=" << hardware.size()
            << '\n';

        result.passed = false;
        result.mismatch_count =
            std::max(golden.size(), hardware.size());

        return result;
    }

    for (std::size_t index = 0; index < golden.size(); ++index) {
        const float expected = golden[index];
        const float actual = hardware[index];

        bool matches = false;
        float absolute_error = 0.0f;
        float relative_error = 0.0f;

        if (expected == actual) {
            matches = true;
        }
        else if (!std::isfinite(expected) ||
                 !std::isfinite(actual)) {
            absolute_error =
                std::numeric_limits<float>::infinity();

            relative_error =
                std::numeric_limits<float>::infinity();

            ++result.non_finite_count;
        }
        else {
            absolute_error = std::fabs(expected - actual);
	    const float allowed_error =
		    absolute_tolerance +
		    relative_tolerance * std::fabs(expected);

	    matches = absolute_error <= allowed_error;

            const float relative_scale =
                std::max(std::fabs(expected), 1.0e-7f);

            relative_error =
                absolute_error / relative_scale;
        }

        if (absolute_error > result.maximum_absolute_error) {
            result.maximum_absolute_error = absolute_error;
            result.maximum_absolute_error_index = index;
        }

        if (relative_error > result.maximum_relative_error) {
            result.maximum_relative_error = relative_error;
            result.maximum_relative_error_index = index;
        }

        if (matches) {
            continue;
        }

        result.passed = false;
        ++result.mismatch_count;

        if (result.mismatch_count <=
            maximum_mismatches_to_print) {
            const std::size_t row = index / MATRIX_SIZE;
            const std::size_t col = index % MATRIX_SIZE;

            std::cout
                << "Mismatch " << result.mismatch_count
                << " at [" << row << "][" << col << "]"
                << " (linear index " << index << "): "
                << std::scientific
                << std::setprecision(9)
                << "golden=" << expected
                << ", hw=" << actual
                << ", abs_error=" << absolute_error
                << ", rel_error=" << relative_error
                << '\n';
        }
    }

    if (result.mismatch_count >
        maximum_mismatches_to_print) {
        std::cout
            << "... "
            << result.mismatch_count -
                   maximum_mismatches_to_print
            << " additional mismatches not printed\n";
    }

    return result;
}

void print_comparison_summary(
    const ComparisonResult& result)
{
    const std::size_t abs_row =
        result.maximum_absolute_error_index / MATRIX_SIZE;

    const std::size_t abs_col =
        result.maximum_absolute_error_index % MATRIX_SIZE;

    const std::size_t rel_row =
        result.maximum_relative_error_index / MATRIX_SIZE;

    const std::size_t rel_col =
        result.maximum_relative_error_index % MATRIX_SIZE;

    std::cout << std::scientific << std::setprecision(9);

    std::cout
        << "\nComparison summary:\n"
        << "  Elements compared:       "
        << NUM_ELEMENTS << '\n'
        << "  Mismatched elements:     "
        << result.mismatch_count << '\n'
        << "  Non-finite mismatches:   "
        << result.non_finite_count << '\n'
        << "  Maximum absolute error:  "
        << result.maximum_absolute_error
        << " at [" << abs_row << "][" << abs_col << "]\n"
        << "  Maximum relative error:  "
        << result.maximum_relative_error
        << " at [" << rel_row << "][" << rel_col << "]\n";
}

template <typename Clock>
double elapsed_ms(
    const std::chrono::time_point<Clock>& start,
    const std::chrono::time_point<Clock>& end)
{
    return std::chrono::duration<double, std::milli>(
        end - start).count();
}

} // namespace

int main(int argc, char* argv[])
{
    /*
     * Arguments:
     *
     *   1. XCLBIN filename
     *   2. Input matrix filename
     *   3. Golden 2D DCT filename
     *   4. Optional device BDF/index
     *   5. Optional kernel/CU name
     *   6. Optional absolute tolerance
     *   7. Optional relative tolerance
     */
    if (argc < 4 || argc > 8) {
        std::cerr
            << "Usage: " << argv[0]
            << " <xclbin>"
            << " <input_matrix.txt>"
            << " <golden_dct_matrix.txt>"
            << " [device]"
            << " [kernel_name]"
            << " [absolute_tolerance]"
            << " [relative_tolerance]\n\n"
            << "Example:\n"
            << "  " << argv[0]
            << " design.xclbin"
            << " input_matrix.txt"
            << " golden_matrix.txt"
            << " c1:00.1"
            << " 'dct2d_accelerator:{dct2d_accelerator_1}'"
            << " 1e-2"
            << " 1e-2\n";

        return 1;
    }

    const std::string xclbin_filename = argv[1];
    const std::string input_filename = argv[2];
    const std::string golden_filename = argv[3];

    const std::string device_identifier =
        argc >= 5 ? argv[4] : "c1:00.1";

    /*
     * Change the default CU name if your XCLBIN uses, for example,
     * dct2d_accelerator_0 instead of dct2d_accelerator_1.
     */
    const std::string kernel_name =
        argc >= 6
            ? argv[5]
            : "dct2d_accelerator:{dct2d_accelerator_0}";

    float absolute_tolerance = 1.0e-2f;
    float relative_tolerance = 1.0e-2f;

    try {
        if (argc >= 7) {
            absolute_tolerance = std::stof(argv[6]);
        }

        if (argc >= 8) {
            relative_tolerance = std::stof(argv[7]);
        }
    }
    catch (const std::exception& error) {
        std::cerr
            << "Error parsing tolerances: "
            << error.what() << '\n';
        return 1;
    }

    if (absolute_tolerance < 0.0f ||
        relative_tolerance < 0.0f) {
        std::cerr << "Error: tolerances cannot be negative\n";
        return 1;
    }

    std::vector<float> input;
    std::vector<float> golden;
    std::vector<float> hardware_output(
        NUM_ELEMENTS,
        0.0f);

    std::cout
        << "Reading " << MATRIX_SIZE << " x "
        << MATRIX_SIZE << " input matrix...\n";

    if (!read_float_matrix(input_filename, input)) {
        return 1;
    }

    std::cout
        << "Reading " << MATRIX_SIZE << " x "
        << MATRIX_SIZE << " golden DCT matrix...\n";

    if (!read_float_matrix(golden_filename, golden)) {
        return 1;
    }

    std::cout
        << "Loaded " << NUM_ELEMENTS
        << " input floats and "
        << NUM_ELEMENTS << " golden floats.\n"
        << "Buffer size: " << NUM_BYTES
        << " bytes per matrix.\n";

    try {
        /*
         * Load the device and XCLBIN.
         */
        std::cout
            << "Opening device "
            << device_identifier << "...\n";

        xrt::device device(device_identifier);

        std::cout
            << "Loading " << xclbin_filename << "...\n";

        xrt::xclbin xclbin(xclbin_filename);

        xrt::uuid xclbin_uuid =
            device.load_xclbin(xclbin);

        /*
         * Print the available kernels/CUs. This helps identify the exact
         * CU name required by the kernel_name argument.
         */
        std::cout << "\nKernels in XCLBIN:\n";

        for (const auto& kernel_info :
             xclbin.get_kernels()) {
            std::cout
                << "  Kernel: "
                << kernel_info.get_name() << '\n';

            for (const auto& cu :
                 kernel_info.get_cus()) {
                std::cout
                    << "    CU: "
                    << cu.get_name() << '\n';
            }
        }

        std::cout
            << "\nOpening kernel "
            << kernel_name << "...\n";

        xrt::kernel dct2d_kernel(
            device,
            xclbin_uuid,
            kernel_name);

        /*
         * buf0 is both input and output.
         *
         * buf1 is an equally sized scratch buffer and does not need to be
         * read by the host.
         *
         * group_id(0) corresponds to buf0.
         * group_id(1) corresponds to buf1.
         */
        xrt::bo buf0_bo(
            device,
            NUM_BYTES,
            xrt::bo::flags::normal,
            dct2d_kernel.group_id(0));

        xrt::bo buf1_bo(
            device,
            NUM_BYTES,
            xrt::bo::flags::normal,
            dct2d_kernel.group_id(1));

        /*
         * Only the memory pointers and scalar size are host-controlled
         * kernel arguments. The AIE streams are connected statically in
         * the XCLBIN and are not set by the host.
         *
         * Argument 0: buf0
         * Argument 1: buf1
         * Argument 2: size = number of elements in one row/column
         */
        xrt::run dct2d_run(dct2d_kernel);

        dct2d_run.set_arg(0, buf0_bo);
        dct2d_run.set_arg(1, buf1_bo);
        dct2d_run.set_arg(
            2,
            static_cast<std::uint32_t>(MATRIX_SIZE));

        using Clock = std::chrono::steady_clock;

        const auto total_start = Clock::now();

        /*
         * Copy input_matrix into buf0 and send it to device memory.
         *
         * buf1 is scratch memory, so no host-to-device initialization is
         * performed. Initialize it if your hardware implementation reads
         * any location before first writing that location.
         */
        std::cout << "Transferring buf0 input to device...\n";

        const auto h2d_start = Clock::now();

        buf0_bo.write(
            input.data(),
            NUM_BYTES,
            0);

        buf0_bo.sync(
            XCL_BO_SYNC_BO_TO_DEVICE,
            NUM_BYTES,
            0);

        const auto h2d_end = Clock::now();

        /*
         * Run the single accelerator kernel.
         */
        std::cout << "Running hardware 2D DCT...\n";

        const auto run_start = Clock::now();

        dct2d_run.start();
        dct2d_run.wait();

        const auto run_end = Clock::now();

        /*
         * The result is stored back into buf0.
         */
        std::cout
            << "Transferring result from buf0 to host...\n";

        const auto d2h_start = Clock::now();

        buf0_bo.sync(
            XCL_BO_SYNC_BO_FROM_DEVICE,
            NUM_BYTES,
            0);

        buf0_bo.read(
            hardware_output.data(),
            NUM_BYTES,
            0);

        const auto d2h_end = Clock::now();
        const auto total_end = Clock::now();

        std::cout
            << "Comparing hardware output with golden 2D DCT...\n"
            << "Absolute tolerance: "
            << absolute_tolerance << '\n'
            << "Relative tolerance: "
            << relative_tolerance << '\n';

        constexpr std::size_t MAX_MISMATCHES_TO_PRINT = 32;

        const ComparisonResult comparison =
            compare_matrices(
                golden,
                hardware_output,
                absolute_tolerance,
                relative_tolerance,
                MAX_MISMATCHES_TO_PRINT);

        print_comparison_summary(comparison);

        std::cout
            << std::fixed
            << std::setprecision(3)
            << "\nTiming:\n"
            << "  Host-to-device transfer: "
            << elapsed_ms(h2d_start, h2d_end)
            << " ms\n"
            << "  Kernel execution:        "
            << elapsed_ms(run_start, run_end)
            << " ms\n"
            << "  Device-to-host transfer: "
            << elapsed_ms(d2h_start, d2h_end)
            << " ms\n"
            << "  Total execution time:    "
            << elapsed_ms(total_start, total_end)
            << " ms\n";

        if (!comparison.passed) {
            std::cout
                << "\nFAIL: hardware output does not match "
                << "the golden 2D DCT output.\n";
            return 1;
        }

        std::cout
            << "\nPASS: hardware output matches "
            << "the golden 2D DCT output.\n";

        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "XRT execution failed: "
            << error.what() << '\n';
        return 1;
    }
}
