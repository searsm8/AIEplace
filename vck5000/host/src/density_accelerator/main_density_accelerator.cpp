#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
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

constexpr std::size_t MATRIX_DIMENSION            = 1024;
constexpr std::size_t NUM_ELEMENTS_PER_MATRIX     = MATRIX_DIMENSION * MATRIX_DIMENSION;
constexpr std::size_t NUM_BYTES_PER_MATRIX        = NUM_ELEMENTS_PER_MATRIX * sizeof(float);

// For indexing a mapped float*
constexpr std::size_t INPUT_ELEMENT_OFFSET        = 0 * NUM_ELEMENTS_PER_MATRIX;
constexpr std::size_t TMP_ELEMENT_OFFSET          = 1 * NUM_ELEMENTS_PER_MATRIX;
constexpr std::size_t FREQ_NO_DC_ELEMENT_OFFSET   = 2 * NUM_ELEMENTS_PER_MATRIX;
constexpr std::size_t POTENTIAL_ELEMENT_OFFSET    = 3 * NUM_ELEMENTS_PER_MATRIX;
constexpr std::size_t FIELD_X_ELEMENT_OFFSET      = 4 * NUM_ELEMENTS_PER_MATRIX;
constexpr std::size_t FIELD_Y_ELEMENT_OFFSET      = 5 * NUM_ELEMENTS_PER_MATRIX;

// For xrt::bo::sync(), read(), and write()
constexpr std::size_t INPUT_BYTE_OFFSET           = INPUT_ELEMENT_OFFSET * sizeof(float);
constexpr std::size_t TMP_BYTE_OFFSET             = TMP_ELEMENT_OFFSET * sizeof(float);
constexpr std::size_t FREQ_NO_DC_BYTE_OFFSET      = FREQ_NO_DC_ELEMENT_OFFSET * sizeof(float);
constexpr std::size_t POTENTIAL_BYTE_OFFSET       = POTENTIAL_ELEMENT_OFFSET * sizeof(float);
constexpr std::size_t FIELD_X_BYTE_OFFSET         = FIELD_X_ELEMENT_OFFSET * sizeof(float);
constexpr std::size_t FIELD_Y_BYTE_OFFSET         = FIELD_Y_ELEMENT_OFFSET * sizeof(float);

constexpr std::size_t BO_TOTAL_NUM_ELEMENTS       = 6 * NUM_ELEMENTS_PER_MATRIX;
constexpr std::size_t BO_NUM_BYTES                = BO_TOTAL_NUM_ELEMENTS * sizeof(float);

static uint32_t float_bits(float x)
{
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    return bits;
}


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
    data.reserve(NUM_ELEMENTS_PER_MATRIX);

    float value = 0.0f;

    // This accepts both space-separated matrices and one value per line.
    while (file >> value) {
        data.push_back(value);

        if (data.size() > NUM_ELEMENTS_PER_MATRIX) {
            std::cerr
                << "Error: " << filename
                << " contains more than " << NUM_ELEMENTS_PER_MATRIX
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

    if (data.size() != NUM_ELEMENTS_PER_MATRIX) {
        std::cerr
            << "Error: " << filename
            << " contains " << data.size()
            << " values; expected exactly "
            << NUM_ELEMENTS_PER_MATRIX << '\n';
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
            const std::size_t row = index / MATRIX_DIMENSION;
            const std::size_t col = index % MATRIX_DIMENSION;

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
        result.maximum_absolute_error_index / MATRIX_DIMENSION;

    const std::size_t abs_col =
        result.maximum_absolute_error_index % MATRIX_DIMENSION;

    const std::size_t rel_row =
        result.maximum_relative_error_index / MATRIX_DIMENSION;

    const std::size_t rel_col =
        result.maximum_relative_error_index % MATRIX_DIMENSION;

    std::cout << std::scientific << std::setprecision(9);

    std::cout
        << "\nComparison summary:\n"
        << "  Elements compared:       "
        << NUM_ELEMENTS_PER_MATRIX << '\n'
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
     *   2. Input matrix
     *   3. Golden frequency matrix with DC removed
     *   4. Golden potential
     *   5. Golden X field
     *   6. Golden Y field
     *   7. Optional device BDF/index
     *   8. Optional kernel/CU name
     *   9. Optional absolute tolerance
     *  10. Optional relative tolerance
     */
    if (argc < 7 || argc > 11) {
        std::cerr
            << "Usage: " << argv[0]
            << " <xclbin>"
            << " <input.txt>"
            << " <golden_freq_no_dc.txt>"
            << " <golden_potential.txt>"
            << " <golden_field_x.txt>"
            << " <golden_field_y.txt>"
            << " [device]"
            << " [kernel_name]"
            << " [absolute_tolerance]"
            << " [relative_tolerance]\n\n"
            << "Example:\n"
            << "  " << argv[0]
            << " design.xclbin"
            << " input.txt"
            << " golden_freq_no_dc.txt"
            << " golden_potential.txt"
            << " golden_field_x.txt"
            << " golden_field_y.txt"
            << " c1:00.1"
            << " 'density_accelerator:{density_accelerator_0}'"
            << " 3e-2"
            << " 1e-2\n";

        return 1;
    }

    const std::string xclbin_filename = argv[1];
    const std::string input_filename = argv[2];
    const std::string golden_freq_filename = argv[3];
    const std::string golden_potential_filename = argv[4];
    const std::string golden_field_x_filename = argv[5];
    const std::string golden_field_y_filename = argv[6];

    const std::string device_identifier =
        argc >= 8 ? argv[7] : "c1:00.1";

    const std::string kernel_name =
        argc >= 9
            ? argv[8]
            : "density_accelerator:{density_accelerator_0}";

    float absolute_tolerance = 3.0e-2f;
    float relative_tolerance = 1.0e-2f;

    try {
        if (argc >= 10) {
            absolute_tolerance = std::stof(argv[9]);
        }

        if (argc >= 11) {
            relative_tolerance = std::stof(argv[10]);
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
    std::vector<float> golden_freq;
    std::vector<float> golden_potential;
    std::vector<float> golden_field_x;
    std::vector<float> golden_field_y;

    std::cout
        << "Reading " << MATRIX_DIMENSION << " x "
        << MATRIX_DIMENSION << " input matrix...\n";

    if (!read_float_matrix(input_filename, input)) {
        return 1;
    }

    std::cout << "Reading golden frequency matrix...\n";
    if (!read_float_matrix(
            golden_freq_filename,
            golden_freq)) {
        return 1;
    }

    std::cout << "Reading golden potential matrix...\n";
    if (!read_float_matrix(
            golden_potential_filename,
            golden_potential)) {
        return 1;
    }

    std::cout << "Reading golden X-field matrix...\n";
    if (!read_float_matrix(
            golden_field_x_filename,
            golden_field_x)) {
        return 1;
    }

    std::cout << "Reading golden Y-field matrix...\n";
    if (!read_float_matrix(
            golden_field_y_filename,
            golden_field_y)) {
        return 1;
    }

    /*
     * Do not divide golden_freq by four here. The new golden-file
     * generator already applies the factor of 1/4 required to match
     * the two-pass hardware DCT convention.
     */

    std::cout
        << "Loaded input and four golden matrices.\n"
        << "Matrix size: "
        << NUM_BYTES_PER_MATRIX
        << " bytes.\n"
        << "Total BO size: "
        << BO_NUM_BYTES
        << " bytes.\n";

    try {
        std::cout
            << "Opening device "
            << device_identifier << "...\n";

        xrt::device device(device_identifier);

        std::cout
            << "Loading "
            << xclbin_filename << "...\n";

        xrt::xclbin xclbin(xclbin_filename);

        const xrt::uuid xclbin_uuid =
            device.load_xclbin(xclbin);

        std::cout << "\nKernels in XCLBIN:\n";

        for (const auto& kernel_info : xclbin.get_kernels()) {
            std::cout
                << "  Kernel: "
                << kernel_info.get_name() << '\n';

            for (const auto& cu : kernel_info.get_cus()) {
                std::cout
                    << "    CU: "
                    << cu.get_name() << '\n';
            }
        }

        std::cout
            << "\nOpening kernel "
            << kernel_name << "...\n";

        xrt::kernel accelerator_kernel(
            device,
            xclbin_uuid,
            kernel_name);

        /*
         * One BO contains:
         *
         *   region 0: input
         *   region 1: temporary matrix
         *   region 2: frequency coefficients, DC removed
         *   region 3: potential
         *   region 4: X field
         *   region 5: Y field
         */
        xrt::bo memory_bo(
            device,
            BO_NUM_BYTES,
            xrt::bo::flags::normal,
            accelerator_kernel.group_id(0));

        xrt::run accelerator_run(accelerator_kernel);

        /*
         * mem_read and mem_write both refer to the same BO.
         */
        accelerator_run.set_arg(0, memory_bo);
        accelerator_run.set_arg(1, memory_bo);
        accelerator_run.set_arg(2, memory_bo);
        accelerator_run.set_arg(3, memory_bo);
        accelerator_run.set_arg(4, memory_bo);
        accelerator_run.set_arg(5, memory_bo);
        accelerator_run.set_arg(6, memory_bo);
        accelerator_run.set_arg(7, memory_bo);
        accelerator_run.set_arg(8, memory_bo);
        accelerator_run.set_arg(9, memory_bo);
        accelerator_run.set_arg(10, memory_bo);
        accelerator_run.set_arg(11, memory_bo);

        float* memory = memory_bo.map<float*>();

        using Clock = std::chrono::steady_clock;

        const auto total_start = Clock::now();

        /*
         * Initialize the BO.
         *
         * Fill all regions with NaN first. This makes an unwritten
         * output region immediately visible as a non-finite mismatch.
         */
        std::cout << "Initializing device buffer...\n";

        const auto h2d_start = Clock::now();

        std::fill(
            memory,
            memory + BO_TOTAL_NUM_ELEMENTS,
            std::numeric_limits<float>::quiet_NaN());

        std::copy(
            input.begin(),
            input.end(),
            memory + INPUT_ELEMENT_OFFSET);

        memory_bo.sync(
            XCL_BO_SYNC_BO_TO_DEVICE,
            BO_NUM_BYTES,
            0);

        const auto h2d_end = Clock::now();

        /*
         * Execute the complete density accelerator.
         */
        std::cout << "Running density accelerator...\n";

        const auto run_start = Clock::now();

        accelerator_run.start();
        accelerator_run.wait();

        const auto run_end = Clock::now();

        /*
         * Retrieve all six regions.
         */
        std::cout << "Transferring results to host...\n";

        const auto d2h_start = Clock::now();

        memory_bo.sync(
            XCL_BO_SYNC_BO_FROM_DEVICE,
            BO_NUM_BYTES,
            0);

        const auto d2h_end = Clock::now();
        const auto total_end = Clock::now();

        /*
         * Copy the four output regions into separate vectors.
         */
        std::vector<float> hardware_freq(
            NUM_ELEMENTS_PER_MATRIX);

        std::vector<float> hardware_potential(
            NUM_ELEMENTS_PER_MATRIX);

        std::vector<float> hardware_field_x(
            NUM_ELEMENTS_PER_MATRIX);

        std::vector<float> hardware_field_y(
            NUM_ELEMENTS_PER_MATRIX);

        std::copy_n(
            memory + FREQ_NO_DC_ELEMENT_OFFSET,
            NUM_ELEMENTS_PER_MATRIX,
            hardware_freq.begin());

        std::copy_n(
            memory + POTENTIAL_ELEMENT_OFFSET,
            NUM_ELEMENTS_PER_MATRIX,
            hardware_potential.begin());

        std::copy_n(
            memory + FIELD_X_ELEMENT_OFFSET,
            NUM_ELEMENTS_PER_MATRIX,
            hardware_field_x.begin());

        std::copy_n(
            memory + FIELD_Y_ELEMENT_OFFSET,
            NUM_ELEMENTS_PER_MATRIX,
            hardware_field_y.begin());

        std::cout
            << "\nComparison tolerances:\n"
            << "  Absolute tolerance: "
            << absolute_tolerance << '\n'
            << "  Relative tolerance: "
            << relative_tolerance << '\n';

        constexpr std::size_t MAX_MISMATCHES_TO_PRINT = 32;

        auto compare_output =
            [&](const char* name,
                const std::vector<float>& golden,
                const std::vector<float>& hardware)
            {
                std::cout
                    << "\n========================================\n"
                    << "Comparing " << name << "\n"
                    << "========================================\n";

                const ComparisonResult result =
                    compare_matrices(
                        golden,
                        hardware,
                        absolute_tolerance,
                        relative_tolerance,
                        MAX_MISMATCHES_TO_PRINT);

                print_comparison_summary(result);

                std::cout
                    << (result.passed ? "PASS: " : "FAIL: ")
                    << name
                    << (result.passed
                            ? " matches the golden output.\n"
                            : " does not match the golden output.\n");

                return result;
            };

        const ComparisonResult frequency_result =
            compare_output(
                "frequency coefficients with DC removed",
                golden_freq,
                hardware_freq);

        const ComparisonResult potential_result =
            compare_output(
                "potential",
                golden_potential,
                hardware_potential);

        const ComparisonResult field_x_result =
            compare_output(
                "X field",
                golden_field_x,
                hardware_field_x);

        const ComparisonResult field_y_result =
            compare_output(
                "Y field",
                golden_field_y,
                hardware_field_y);

        const bool all_passed =
            frequency_result.passed &&
            potential_result.passed &&
            field_x_result.passed &&
            field_y_result.passed;

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

        std::cout
            << "\nOverall result: "
            << (all_passed ? "PASS" : "FAIL")
            << '\n';

        return all_passed ? 0 : 1;
    }
    catch (const std::exception& error) {
        std::cerr
            << "XRT execution failed: "
            << error.what() << '\n';
        return 1;
    }
}
