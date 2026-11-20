#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

#include <fftw3.h>

namespace {

constexpr int ROWS = 1024;
constexpr int COLS = 1024;

constexpr std::size_t ELEMENTS =
    static_cast<std::size_t>(ROWS) *
    static_cast<std::size_t>(COLS);

constexpr float PI =
    3.1415926535897932384626433832795f;

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
    return std::cos(
        PI *
        (static_cast<float>(n) + 0.5f) *
        static_cast<float>(k) /
        static_cast<float>(size));
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

            // This constant offset will be removed by zeroing DCT[0][0].
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

    // Enough digits to round-trip a float exactly.
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

} // namespace

int main()
{
    float* input = static_cast<float*>(
        fftwf_malloc(sizeof(float) * ELEMENTS));

    float* dct = static_cast<float*>(
        fftwf_malloc(sizeof(float) * ELEMENTS));

    float* reconstructed = static_cast<float*>(
        fftwf_malloc(sizeof(float) * ELEMENTS));

    if (input == nullptr ||
        dct == nullptr ||
        reconstructed == nullptr) {
        std::cerr << "Error: FFTW memory allocation failed\n";

        if (input != nullptr) {
            fftwf_free(input);
        }

        if (dct != nullptr) {
            fftwf_free(dct);
        }

        if (reconstructed != nullptr) {
            fftwf_free(reconstructed);
        }

        return 1;
    }

    /*
     * Forward 2D DCT-II.
     *
     * FFTW_REDFT10 is FFTW's unnormalized DCT-II.
     */
    fftwf_plan forward_plan = fftwf_plan_r2r_2d(
        ROWS,
        COLS,
        input,
        dct,
        FFTW_REDFT10,
        FFTW_REDFT10,
        FFTW_ESTIMATE);

    /*
     * Inverse of the DCT-II.
     *
     * FFTW_REDFT01 is FFTW's unnormalized DCT-III.
     */
    fftwf_plan inverse_plan = fftwf_plan_r2r_2d(
        ROWS,
        COLS,
        dct,
        reconstructed,
        FFTW_REDFT01,
        FFTW_REDFT01,
        FFTW_ESTIMATE);

    if (forward_plan == nullptr || inverse_plan == nullptr) {
        std::cerr << "Error: FFTW plan creation failed\n";

        if (forward_plan != nullptr) {
            fftwf_destroy_plan(forward_plan);
        }

        if (inverse_plan != nullptr) {
            fftwf_destroy_plan(inverse_plan);
        }

        fftwf_free(input);
        fftwf_free(dct);
        fftwf_free(reconstructed);

        return 1;
    }

    // Generate the input after creating the plans because some FFTW planning
    // modes are allowed to modify the supplied buffers.
    std::cout << "Generating input matrix...\n";
    generate_input(input);

    const double input_mean = calculate_mean(input);

    std::cout << "Writing input_matrix.txt...\n";

    if (!write_matrix(
            "input_matrix.txt",
            input,
            ROWS,
            COLS)) {
        fftwf_destroy_plan(forward_plan);
        fftwf_destroy_plan(inverse_plan);
        fftwf_free(input);
        fftwf_free(dct);
        fftwf_free(reconstructed);
        return 1;
    }

    std::cout << "Executing forward 2D DCT-II...\n";
    fftwf_execute(forward_plan);

    const float original_dc = dct[index_2d(0, 0)];

    /*
     * Remove the global DC component.
     *
     * Only [0][0] is the global DC coefficient. The rest of row zero and
     * column zero contain valid one-dimensional spatial frequencies.
     */
    dct[index_2d(0, 0)] = 0.0f;

    std::cout << "Writing dct_no_dc_matrix.txt...\n";

    if (!write_matrix(
            "dct_no_dc_matrix.txt",
            dct,
            ROWS,
            COLS)) {
        fftwf_destroy_plan(forward_plan);
        fftwf_destroy_plan(inverse_plan);
        fftwf_free(input);
        fftwf_free(dct);
        fftwf_free(reconstructed);
        return 1;
    }

    std::cout << "Executing inverse 2D DCT-III...\n";
    fftwf_execute(inverse_plan);

    /*
     * FFTW's REDFT10 and REDFT01 are unnormalized.
     *
     * For one dimension of length N:
     *
     *     REDFT01(REDFT10(x)) = 2*N*x
     *
     * For a ROWS x COLS 2D transform:
     *
     *     inverse(forward(x)) = (2*ROWS)*(2*COLS)*x
     *                         = 4*ROWS*COLS*x
     *
     * Therefore, divide every reconstructed sample by
     * 4*ROWS*COLS.
     */
    const float inverse_scale =
        1.0f /
        (4.0f *
         static_cast<float>(ROWS) *
         static_cast<float>(COLS));

    for (std::size_t i = 0; i < ELEMENTS; ++i) {
        reconstructed[i] *= inverse_scale;
    }

    const double reconstructed_mean =
        calculate_mean(reconstructed);

    std::cout << "Writing golden_matrix.txt...\n";

    if (!write_matrix(
            "golden_matrix.txt",
            reconstructed,
            ROWS,
            COLS)) {
        fftwf_destroy_plan(forward_plan);
        fftwf_destroy_plan(inverse_plan);
        fftwf_free(input);
        fftwf_free(dct);
        fftwf_free(reconstructed);
        return 1;
    }

    std::cout << std::scientific
              << std::setprecision(9);

    std::cout << "\nResults:\n";
    std::cout << "Input mean             : "
              << input_mean << '\n';

    std::cout << "Original DCT[0][0]     : "
              << original_dc << '\n';

    std::cout << "DCT[0][0] after removal: "
              << dct[index_2d(0, 0)] << '\n';

    std::cout << "Reconstructed mean     : "
              << reconstructed_mean << '\n';

    std::cout << "\nGenerated files:\n"
              << "  input_matrix.txt\n"
              << "  dct_no_dc_matrix.txt\n"
              << "  golden_matrix.txt\n";

    fftwf_destroy_plan(forward_plan);
    fftwf_destroy_plan(inverse_plan);

    fftwf_free(input);
    fftwf_free(dct);
    fftwf_free(reconstructed);

    return 0;
}
