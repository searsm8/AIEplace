#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

#include <fftw3.h>

namespace {

constexpr int ROWS = 1024;
constexpr int COLS = 1024;

constexpr std::size_t NUM_ELEMENTS =
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

float clamp(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

/*
 * Cubic smoothstep:
 *
 *     0 at x <= 0
 *     1 at x >= 1
 *     smooth transition between 0 and 1
 */
float smoothstep(float x)
{
    x = clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

std::uint32_t xorshift32(std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float random_signed_float(std::uint32_t& state)
{
    const std::uint32_t bits = xorshift32(state) >> 8;

    const float value_0_to_1 =
        static_cast<float>(bits) *
        (1.0f / 16777216.0f);

    return 2.0f * value_0_to_1 - 1.0f;
}

/*
 * DCT-II basis function:
 *
 *     cos(pi * (n + 0.5) * k / N)
 */
float dct_basis(int n, int k, int size)
{
    return std::cos(
        PI *
        (static_cast<float>(n) + 0.5f) *
        static_cast<float>(k) /
        static_cast<float>(size));
}

/*
 * A circular region centered in the matrix.
 *
 * The circle has:
 *
 *     value 1.0 for distance <= INNER_RADIUS
 *     smooth transition from 1.0 to 0.0 in the feather region
 *     value 0.0 for distance >= OUTER_RADIUS
 *
 * The center is (511.5, 511.5), which makes the pattern exactly symmetric
 * for an even-sized 1024 x 1024 matrix.
 */
void generate_soft_circle(float* matrix)
{
    constexpr float CENTER_ROW =
        (static_cast<float>(ROWS) - 1.0f) / 2.0f;

    constexpr float CENTER_COL =
        (static_cast<float>(COLS) - 1.0f) / 2.0f;

    constexpr float INNER_RADIUS = 240.0f;
    constexpr float OUTER_RADIUS = 320.0f;

    constexpr float BACKGROUND_VALUE = 0.0f;
    constexpr float INTERIOR_VALUE = 1.0f;

    for (int row = 0; row < ROWS; ++row) {
        const float dy =
            static_cast<float>(row) - CENTER_ROW;

        for (int col = 0; col < COLS; ++col) {
            const float dx =
                static_cast<float>(col) - CENTER_COL;

            const float distance =
                std::sqrt(dx * dx + dy * dy);

            float circle_weight;

            if (distance <= INNER_RADIUS) {
                circle_weight = 1.0f;
            }
            else if (distance >= OUTER_RADIUS) {
                circle_weight = 0.0f;
            }
            else {
                const float edge_position =
                    (distance - INNER_RADIUS) /
                    (OUTER_RADIUS - INNER_RADIUS);

                circle_weight =
                    1.0f - smoothstep(edge_position);
            }

            matrix[index_2d(row, col)] =
                BACKGROUND_VALUE +
                circle_weight *
                    (INTERIOR_VALUE - BACKGROUND_VALUE);
        }
    }
}

/*
 * A radial gradient:
 *
 *     value 1.0 at the matrix center
 *     value decreases smoothly toward the edge
 *     value 0.0 outside the radius
 */
void generate_radial_gradient(float* matrix)
{
    constexpr float CENTER_ROW =
        (static_cast<float>(ROWS) - 1.0f) / 2.0f;

    constexpr float CENTER_COL =
        (static_cast<float>(COLS) - 1.0f) / 2.0f;

    constexpr float RADIUS = 340.0f;

    for (int row = 0; row < ROWS; ++row) {
        const float dy =
            static_cast<float>(row) - CENTER_ROW;

        for (int col = 0; col < COLS; ++col) {
            const float dx =
                static_cast<float>(col) - CENTER_COL;

            const float distance =
                std::sqrt(dx * dx + dy * dy);

            const float normalized_distance =
                clamp(distance / RADIUS, 0.0f, 1.0f);

            // Smoothly fades from one at the center to zero at the radius.
            matrix[index_2d(row, col)] =
                1.0f - smoothstep(normalized_distance);
        }
    }
}

/*
 * A single impulse near the center.
 *
 * For an even-sized matrix there are four samples surrounding the exact
 * geometric center. This test places the impulse at [512][512].
 */
void generate_impulse(float* matrix)
{
    std::fill(matrix, matrix + NUM_ELEMENTS, 0.0f);
    matrix[index_2d(ROWS / 2, COLS / 2)] = 1.0f;
}

/*
 * Alternating 32 x 32 blocks with values -1 and +1.
 */
void generate_checkerboard(float* matrix)
{
    constexpr int BLOCK_SIZE = 32;

    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ++col) {
            const int block_row = row / BLOCK_SIZE;
            const int block_col = col / BLOCK_SIZE;

            const bool high =
                ((block_row + block_col) & 1) == 0;

            matrix[index_2d(row, col)] =
                high ? 1.0f : -1.0f;
        }
    }
}

/*
 * Deterministic random matrix in [-1, 1).
 *
 * The fixed seed ensures that every run generates the same test data.
 */
void generate_random(float* matrix)
{
    std::uint32_t state = 0x12345678u;

    for (std::size_t i = 0; i < NUM_ELEMENTS; ++i) {
        matrix[i] = random_signed_float(state);
    }
}

/*
 * A single 2D DCT basis function.
 *
 * Most of the DCT energy should appear at coefficient [13][29].
 */
void generate_dct_mode(float* matrix)
{
    constexpr int ROW_FREQUENCY = 13;
    constexpr int COL_FREQUENCY = 29;

    for (int row = 0; row < ROWS; ++row) {
        const float row_component =
            dct_basis(row, ROW_FREQUENCY, ROWS);

        for (int col = 0; col < COLS; ++col) {
            const float col_component =
                dct_basis(col, COL_FREQUENCY, COLS);

            matrix[index_2d(row, col)] =
                row_component * col_component;
        }
    }
}

/*
 * A constant input tests the global DC coefficient.
 *
 * Ideally only DCT[0][0] is nonzero, apart from floating-point rounding.
 */
void generate_constant(float* matrix)
{
    std::fill(matrix, matrix + NUM_ELEMENTS, 1.0f);
}

/*
 * A mixture of horizontal and vertical DCT basis patterns.
 *
 * The main coefficients should be [7][0] and [0][23].
 */
void generate_stripes(float* matrix)
{
    constexpr int ROW_FREQUENCY = 7;
    constexpr int COL_FREQUENCY = 23;

    for (int row = 0; row < ROWS; ++row) {
        const float vertical =
            dct_basis(row, ROW_FREQUENCY, ROWS);

        for (int col = 0; col < COLS; ++col) {
            const float horizontal =
                dct_basis(col, COL_FREQUENCY, COLS);

            matrix[index_2d(row, col)] =
                0.6f * vertical +
                0.4f * horizontal;
        }
    }
}

bool generate_pattern(
    const std::string& pattern,
    float* matrix)
{
    if (pattern == "circle") {
        generate_soft_circle(matrix);
    }
    else if (pattern == "radial") {
        generate_radial_gradient(matrix);
    }
    else if (pattern == "impulse") {
        generate_impulse(matrix);
    }
    else if (pattern == "checkerboard") {
        generate_checkerboard(matrix);
    }
    else if (pattern == "random") {
        generate_random(matrix);
    }
    else if (pattern == "dct_mode") {
        generate_dct_mode(matrix);
    }
    else if (pattern == "constant") {
        generate_constant(matrix);
    }
    else if (pattern == "stripes") {
        generate_stripes(matrix);
    }
    else {
        std::cerr
            << "Error: unknown pattern \"" << pattern << "\"\n";
        return false;
    }

    return true;
}

bool write_matrix(
    const std::string& filename,
    const float* matrix)
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

    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ++col) {
            if (col != 0) {
                file << ' ';
            }

            file << matrix[index_2d(row, col)];
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

void print_pattern_list()
{
    std::cerr
        << "Available patterns:\n"
        << "  circle       Flat circular center with a smooth edge\n"
        << "  radial       Radial gradient from one to zero\n"
        << "  impulse      Single impulse at [512][512]\n"
        << "  checkerboard Alternating 32 x 32 blocks\n"
        << "  random       Deterministic random values in [-1,1]\n"
        << "  dct_mode     Single 2D DCT mode at [13][29]\n"
        << "  constant     Constant matrix containing ones\n"
        << "  stripes      Horizontal and vertical DCT modes\n";
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr
            << "Usage: " << argv[0]
            << " <pattern> <output_prefix>\n\n"
            << "Example:\n"
            << "  " << argv[0]
            << " circle test_data/circle\n\n";

        print_pattern_list();
        return 1;
    }

    const std::string pattern = argv[1];
    const std::string output_prefix = argv[2];

    const std::string input_filename =
        output_prefix + "_input_matrix.txt";

    const std::string golden_filename =
        output_prefix + "_golden_matrix.txt";

    float* input = static_cast<float*>(
        fftwf_malloc(sizeof(float) * NUM_ELEMENTS));

    float* output = static_cast<float*>(
        fftwf_malloc(sizeof(float) * NUM_ELEMENTS));

    if (input == nullptr || output == nullptr) {
        std::cerr << "Error: FFTW memory allocation failed\n";

        if (input != nullptr) {
            fftwf_free(input);
        }

        if (output != nullptr) {
            fftwf_free(output);
        }

        return 1;
    }

    /*
     * Unnormalized two-dimensional DCT-II.
     *
     * This matches FFTW_REDFT10 in both matrix dimensions.
     */
    fftwf_plan plan = fftwf_plan_r2r_2d(
        ROWS,
        COLS,
        input,
        output,
        FFTW_REDFT10,
        FFTW_REDFT10,
        FFTW_ESTIMATE);

    if (plan == nullptr) {
        std::cerr << "Error: FFTW plan creation failed\n";
        fftwf_free(input);
        fftwf_free(output);
        return 1;
    }

    /*
     * Generate the matrix after plan creation because FFTW planning modes
     * are permitted to modify the supplied buffers.
     */
    std::cout
        << "Generating pattern: "
        << pattern << '\n';

    if (!generate_pattern(pattern, input)) {
        print_pattern_list();
        fftwf_destroy_plan(plan);
        fftwf_free(input);
        fftwf_free(output);
        return 1;
    }

    std::cout
        << "Writing input matrix to "
        << input_filename << "...\n";

    if (!write_matrix(input_filename, input)) {
        fftwf_destroy_plan(plan);
        fftwf_free(input);
        fftwf_free(output);
        return 1;
    }

    std::cout
        << "Executing unnormalized 2D DCT-II...\n";

    fftwf_execute(plan);

    std::cout
        << "Writing golden matrix to "
        << golden_filename << "...\n";

    if (!write_matrix(golden_filename, output)) {
        fftwf_destroy_plan(plan);
        fftwf_free(input);
        fftwf_free(output);
        return 1;
    }

    std::cout << std::scientific
              << std::setprecision(9);

    std::cout
        << "\nSelected coefficients:\n"
        << "  DCT[0][0]   = "
        << output[index_2d(0, 0)] << '\n'
        << "  DCT[0][1]   = "
        << output[index_2d(0, 1)] << '\n'
        << "  DCT[1][0]   = "
        << output[index_2d(1, 0)] << '\n'
        << "  DCT[13][29] = "
        << output[index_2d(13, 29)] << '\n';

    fftwf_destroy_plan(plan);
    fftwf_free(input);
    fftwf_free(output);

    std::cout
        << "\nGenerated:\n"
        << "  " << input_filename << '\n'
        << "  " << golden_filename << '\n';

    return 0;
}
