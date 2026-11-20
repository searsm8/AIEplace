#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#include <fftw3.h>

namespace {

constexpr int ROWS = 1024;
constexpr int COLS = 1024;

static_assert(ROWS == COLS,
              "This test generator currently assumes a square matrix.");

constexpr int MATRIX_DIMENSION = ROWS;

constexpr std::size_t ELEMENTS =
    static_cast<std::size_t>(ROWS) *
    static_cast<std::size_t>(COLS);

constexpr double PI =
    3.141592653589793238462643383279502884;

std::size_t index_2d(int row, int col)
{
    return static_cast<std::size_t>(row) *
               static_cast<std::size_t>(COLS) +
           static_cast<std::size_t>(col);
}

std::uint32_t xorshift32(std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Deterministic pseudo-random number in approximately [-1, 1).
float random_signed_float(std::uint32_t& state)
{
    const std::uint32_t value = xorshift32(state) >> 8;

    const float unit =
        static_cast<float>(value) *
        (1.0f / 16777216.0f);

    return 2.0f * unit - 1.0f;
}

// DCT-II basis:
//
// cos(pi * (n + 0.5) * k / N)
float dct_basis(int n, int k, int size)
{
    return static_cast<float>(
        std::cos(
            PI *
            (static_cast<double>(n) + 0.5) *
            static_cast<double>(k) /
            static_cast<double>(size)));
}

void generate_input(float* matrix)
{
    std::uint32_t random_state = 0x12345678u;

    for (int row = 0; row < ROWS; ++row) {
        const float row_basis_5 =
            dct_basis(row, 5, ROWS);

        const float row_basis_13 =
            dct_basis(row, 13, ROWS);

        const float row_basis_71 =
            dct_basis(row, 71, ROWS);

        for (int col = 0; col < COLS; ++col) {
            const float col_basis_20 =
                dct_basis(col, 20, COLS);

            const float col_basis_29 =
                dct_basis(col, 29, COLS);

            const float col_basis_37 =
                dct_basis(col, 37, COLS);

            // Removed by setting frequency coefficient [0][0] to zero.
            const float dc_component = 0.5f;

            const float vertical_component =
                0.25f * row_basis_5;

            const float horizontal_component =
                -0.20f * col_basis_20;

            const float mode_13_29 =
                0.35f * row_basis_13 * col_basis_29;

            const float mode_71_37 =
                -0.15f * row_basis_71 * col_basis_37;

            const float noise =
                0.01f * random_signed_float(random_state);

            matrix[index_2d(row, col)] =
                dc_component +
                vertical_component +
                horizontal_component +
                mode_13_29 +
                mode_71_37 +
                noise;
        }
    }
}

bool write_matrix(
    const char* filename,
    const float* matrix,
    int rows,
    int cols)
{
    std::ofstream file(filename);

    if (!file) {
        std::cerr
            << "Error: could not open "
            << filename << '\n';
        return false;
    }

    file << std::scientific
         << std::setprecision(
                std::numeric_limits<float>::max_digits10);

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            if (col != 0) {
                file << ' ';
            }

            const std::size_t index =
                static_cast<std::size_t>(row) *
                    static_cast<std::size_t>(cols) +
                static_cast<std::size_t>(col);

            file << matrix[index];
        }

        file << '\n';
    }

    if (!file) {
        std::cerr
            << "Error while writing "
            << filename << '\n';
        return false;
    }

    return true;
}

double calculate_mean(const float* matrix)
{
    double sum = 0.0;

    for (std::size_t i = 0; i < ELEMENTS; ++i) {
        sum += static_cast<double>(matrix[i]);
    }

    return sum / static_cast<double>(ELEMENTS);
}

void transpose_matrix(
    const std::vector<float>& input,
    std::vector<float>& output)
{
    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ++col) {
            output[index_2d(col, row)] =
                input[index_2d(row, col)];
        }
    }
}

enum class InverseTransform {
    IDCT,
    IDXST
};

/*
 * Apply either the hardware-normalized IDCT or IDXST independently
 * to every row.
 *
 * Hardware IDCT convention:
 *
 *     IDCT(x) = FFTW_REDFT01(x) / N
 *
 * This is the inverse of the hardware DCT convention:
 *
 *     DCT(x) = FFTW_REDFT10(x) / 2
 *
 * IDXST convention:
 *
 *     IDXST(x)[k] = (-1)^k IDCT(q)[k]
 *
 * where:
 *
 *     q[0] = 0
 *     q[n] = x[N-n], n = 1,...,N-1
 */
void transform_rows(
    const std::vector<float>& input,
    std::vector<float>& output,
    InverseTransform transform,
    fftwf_plan inverse_1d_plan,
    std::vector<float>& row_input,
    std::vector<float>& row_output)
{
    constexpr float inverse_scale =
        1.0f / static_cast<float>(MATRIX_DIMENSION);

    for (int row = 0; row < ROWS; ++row) {
        const float* source =
            input.data() +
            static_cast<std::size_t>(row) * COLS;

        if (transform == InverseTransform::IDCT) {
            std::copy(
                source,
                source + COLS,
                row_input.begin());
        } else {
            /*
             * q[n] = x[N-n], with x[N] = 0:
             *
             * q[0]   = 0
             * q[1]   = x[N-1]
             * ...
             * q[N-1] = x[1]
             */
            row_input[0] = 0.0f;

            for (int n = 1; n < COLS; ++n) {
                row_input[n] = source[COLS - n];
            }
        }

        fftwf_execute_r2r(
            inverse_1d_plan,
            row_input.data(),
            row_output.data());

        float* destination =
            output.data() +
            static_cast<std::size_t>(row) * COLS;

        for (int k = 0; k < COLS; ++k) {
            float value =
                row_output[k] * inverse_scale;

            if (transform == InverseTransform::IDXST &&
                (k & 1) != 0) {
                value = -value;
            }

            destination[k] = value;
        }
    }
}

/*
 * Apply a separable 2D inverse transform:
 *
 *     result = second(first(input)^T)^T
 *
 * The first transform operates along x/u (matrix columns).
 * The second transform operates along y/v after transposition.
 */
void separable_inverse_transform(
    const std::vector<float>& input,
    std::vector<float>& output,
    InverseTransform first_transform,
    InverseTransform second_transform,
    fftwf_plan inverse_1d_plan,
    std::vector<float>& row_input,
    std::vector<float>& row_output,
    std::vector<float>& work_1,
    std::vector<float>& work_2,
    std::vector<float>& work_3)
{
    // First transform along the original matrix columns.
    transform_rows(
        input,
        work_1,
        first_transform,
        inverse_1d_plan,
        row_input,
        row_output);

    // Transpose so the second dimension becomes contiguous.
    transpose_matrix(work_1, work_2);

    // Second transform along the original matrix rows.
    transform_rows(
        work_2,
        work_3,
        second_transform,
        inverse_1d_plan,
        row_input,
        row_output);

    // Restore the original matrix orientation.
    transpose_matrix(work_3, output);
}

/*
 * Construct:
 *
 *     field_x_coeff[v][u]
 *         = a[v][u] * wu / (wu^2 + wv^2)
 *
 *     field_y_coeff[v][u]
 *         = a[v][u] * wv / (wu^2 + wv^2)
 *
 * where:
 *
 *     wi = 2*pi*i/M.
 *
 * The undefined (0,0) term is set to zero.
 */
void prepare_field_coefficients(
    const std::vector<float>& frequency,
    std::vector<float>& field_x_coefficients,
    std::vector<float>& field_y_coefficients)
{
    std::vector<float> omega(MATRIX_DIMENSION);
    std::vector<float> omega_squared(MATRIX_DIMENSION);

    for (int i = 0; i < MATRIX_DIMENSION; ++i) {
        const double value =
            2.0 * PI *
            static_cast<double>(i) /
            static_cast<double>(MATRIX_DIMENSION);

        omega[i] = static_cast<float>(value);
        omega_squared[i] =
            static_cast<float>(value * value);
    }

    for (int v = 0; v < ROWS; ++v) {
        const float wv = omega[v];
        const float wv_squared = omega_squared[v];

        for (int u = 0; u < COLS; ++u) {
            const std::size_t index = index_2d(v, u);
            const float coefficient = frequency[index];

            if (u == 0 && v == 0) {
                field_x_coefficients[index] = 0.0f;
                field_y_coefficients[index] = 0.0f;
                continue;
            }

            const float wu = omega[u];

            const float denominator =
                omega_squared[u] + wv_squared;

            field_x_coefficients[index] =
                coefficient * wu / denominator;

            field_y_coefficients[index] =
                coefficient * wv / denominator;
        }
    }
}

float maximum_absolute_difference_from_mean_removed_input(
    const std::vector<float>& input,
    const std::vector<float>& potential)
{
    const double mean = calculate_mean(input.data());

    float maximum_error = 0.0f;

    for (std::size_t i = 0; i < ELEMENTS; ++i) {
        const float expected =
            input[i] - static_cast<float>(mean);

        maximum_error = std::max(
            maximum_error,
            std::abs(potential[i] - expected));
    }

    return maximum_error;
}

} // namespace

int main()
{
    std::vector<float> input(ELEMENTS);
    std::vector<float> fftw_dct(ELEMENTS);
    std::vector<float> frequency_no_dc(ELEMENTS);

    std::vector<float> potential(ELEMENTS);

    std::vector<float> field_x_coefficients(ELEMENTS);
    std::vector<float> field_y_coefficients(ELEMENTS);

    std::vector<float> field_x(ELEMENTS);
    std::vector<float> field_y(ELEMENTS);

    // Reusable scratch matrices.
    std::vector<float> work_1(ELEMENTS);
    std::vector<float> work_2(ELEMENTS);
    std::vector<float> work_3(ELEMENTS);

    // Reusable row buffers for FFTW's 1D inverse transform.
    std::vector<float> row_input(MATRIX_DIMENSION);
    std::vector<float> row_output(MATRIX_DIMENSION);

    /*
     * Forward FFTW 2D DCT-II.
     *
     * FFTW_REDFT10 is FFTW's unnormalized DCT-II. The hardware
     * DCT convention is REDFT10/2 per dimension, so the 2D hardware
     * result is this FFTW result divided by four.
     */
    fftwf_plan forward_2d_plan = fftwf_plan_r2r_2d(
        ROWS,
        COLS,
        input.data(),
        fftw_dct.data(),
        FFTW_REDFT10,
        FFTW_REDFT10,
        FFTW_ESTIMATE | FFTW_UNALIGNED);

    /*
     * One-dimensional unnormalized DCT-III. It is reused for both
     * hardware-normalized IDCT and IDXST.
     */
    fftwf_plan inverse_1d_plan = fftwf_plan_r2r_1d(
        MATRIX_DIMENSION,
        row_input.data(),
        row_output.data(),
        FFTW_REDFT01,
        FFTW_ESTIMATE | FFTW_UNALIGNED);

    if (forward_2d_plan == nullptr ||
        inverse_1d_plan == nullptr) {
        std::cerr << "Error: FFTW plan creation failed\n";

        if (forward_2d_plan != nullptr) {
            fftwf_destroy_plan(forward_2d_plan);
        }

        if (inverse_1d_plan != nullptr) {
            fftwf_destroy_plan(inverse_1d_plan);
        }

        return 1;
    }

    /*
     * FFTW planning modes are allowed to modify their input arrays,
     * so generate the actual input after creating the plans.
     */
    std::cout << "Generating input matrix...\n";
    generate_input(input.data());

    const double input_mean =
        calculate_mean(input.data());

    std::cout << "Executing forward 2D DCT-II...\n";
    fftwf_execute(forward_2d_plan);

    /*
     * The PL/AIE DCT stores:
     *
     *     hardware_1d_DCT = FFTW_REDFT10 / 2
     *
     * Two dimensions therefore produce:
     *
     *     hardware_2d_DCT = FFTW_2D_DCT / 4
     */
    constexpr float forward_2d_scale = 0.25f;

    for (std::size_t i = 0; i < ELEMENTS; ++i) {
        frequency_no_dc[i] =
            fftw_dct[i] * forward_2d_scale;
    }

    const float original_hardware_dc =
        frequency_no_dc[index_2d(0, 0)];

    // Remove the global DC coefficient.
    frequency_no_dc[index_2d(0, 0)] = 0.0f;

    /*
     * Potential:
     *
     *     potential = IDCT(IDCT(a)^T)^T
     */
    std::cout << "Computing potential...\n";

    separable_inverse_transform(
        frequency_no_dc,
        potential,
        InverseTransform::IDCT,
        InverseTransform::IDCT,
        inverse_1d_plan,
        row_input,
        row_output,
        work_1,
        work_2,
        work_3);

    /*
     * Prepare:
     *
     *     ax[u,v] = a[u,v] * wu / (wu^2 + wv^2)
     *     ay[u,v] = a[u,v] * wv / (wu^2 + wv^2)
     */
    std::cout << "Preparing field coefficients...\n";

    prepare_field_coefficients(
        frequency_no_dc,
        field_x_coefficients,
        field_y_coefficients);

    /*
     * Equation (7c):
     *
     *     field_x = IDXST(IDCT(ax)^T)^T
     *
     * First transform along u/x: IDCT.
     * Second transform along v/y: IDXST.
     */
    std::cout << "Computing field X...\n";

    separable_inverse_transform(
        field_x_coefficients,
        field_x,
        InverseTransform::IDXST,
        InverseTransform::IDCT,
        inverse_1d_plan,
        row_input,
        row_output,
        work_1,
        work_2,
        work_3);

    /*
     * Equation (7d):
     *
     *     field_y = IDCT(IDXST(ay)^T)^T
     *
     * First transform along u/x: IDXST.
     * Second transform along v/y: IDCT.
     */
    std::cout << "Computing field Y...\n";

    separable_inverse_transform(
        field_y_coefficients,
        field_y,
        InverseTransform::IDCT,
        InverseTransform::IDXST,
        inverse_1d_plan,
        row_input,
        row_output,
        work_1,
        work_2,
        work_3);

    std::cout << "Writing input.txt...\n";
    if (!write_matrix(
            "input.txt",
            input.data(),
            ROWS,
            COLS)) {
        fftwf_destroy_plan(forward_2d_plan);
        fftwf_destroy_plan(inverse_1d_plan);
        return 1;
    }

    std::cout << "Writing golden_freq_no_dc.txt...\n";
    if (!write_matrix(
            "golden_freq_no_dc.txt",
            frequency_no_dc.data(),
            ROWS,
            COLS)) {
        fftwf_destroy_plan(forward_2d_plan);
        fftwf_destroy_plan(inverse_1d_plan);
        return 1;
    }

    std::cout << "Writing golden_potential.txt...\n";
    if (!write_matrix(
            "golden_potential.txt",
            potential.data(),
            ROWS,
            COLS)) {
        fftwf_destroy_plan(forward_2d_plan);
        fftwf_destroy_plan(inverse_1d_plan);
        return 1;
    }

    std::cout << "Writing golden_field_x.txt...\n";
    if (!write_matrix(
            "golden_field_x.txt",
            field_x.data(),
            ROWS,
            COLS)) {
        fftwf_destroy_plan(forward_2d_plan);
        fftwf_destroy_plan(inverse_1d_plan);
        return 1;
    }

    std::cout << "Writing golden_field_y.txt...\n";
    if (!write_matrix(
            "golden_field_y.txt",
            field_y.data(),
            ROWS,
            COLS)) {
        fftwf_destroy_plan(forward_2d_plan);
        fftwf_destroy_plan(inverse_1d_plan);
        return 1;
    }

    const double potential_mean =
        calculate_mean(potential.data());

    const double field_x_mean =
        calculate_mean(field_x.data());

    const double field_y_mean =
        calculate_mean(field_y.data());

    const float potential_reconstruction_error =
        maximum_absolute_difference_from_mean_removed_input(
            input,
            potential);

    std::cout << std::scientific
              << std::setprecision(9);

    std::cout << "\nResults:\n";

    std::cout << "Input mean                    : "
              << input_mean << '\n';

    std::cout << "Hardware-scaled DCT DC before removal: "
              << original_hardware_dc << '\n';

    std::cout << "DCT DC after removal          : "
              << frequency_no_dc[index_2d(0, 0)] << '\n';

    std::cout << "Potential mean                : "
              << potential_mean << '\n';

    std::cout << "Field X mean                  : "
              << field_x_mean << '\n';

    std::cout << "Field Y mean                  : "
              << field_y_mean << '\n';

    std::cout
        << "Max |potential-(input-mean)|  : "
        << potential_reconstruction_error << '\n';

    std::cout << "\nGenerated files:\n"
              << "  input.txt\n"
              << "  golden_freq_no_dc.txt\n"
              << "  golden_potential.txt\n"
              << "  golden_field_x.txt\n"
              << "  golden_field_y.txt\n";

    fftwf_destroy_plan(forward_2d_plan);
    fftwf_destroy_plan(inverse_1d_plan);

    return 0;
}
