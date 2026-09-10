/**
 * @file Common.h
 * @brief Project-wide includes, aliases, compile-time constants, and the core XY / Position /
 *        Gradient value types shared across the placer.
 */
#pragma once

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <numeric>
#include <math.h>
#include <sys/time.h>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::map;
using std::max;
using std::min;


// ePlace hyperparameters
constexpr int INITIAL_LOCAL_DENSITY_WEIGHT = 1; // initial local density weight for each bin

#define AIEPLACE_NAMESPACE_BEGIN namespace AIEplace {
#define AIEPLACE_NAMESPACE_END }

#define AIEPLACE_VERSION "v0.0.2"
#define stringify std::to_string // alias

AIEPLACE_NAMESPACE_BEGIN 

typedef float position_type;

#define MIN_TOL 0.015
// Contains XY data, which might be coordinates or any other pair of data.
struct XY
{
    float x;
    float y;
    void clear() { x = 0.0f; y = 0.0f;}

    // default constructor initializes to 0,0
    XY () { clear(); }
    XY (float x_val, float y_val) : x(x_val), y(y_val) {}

    bool isClose(XY other)
    {
        float diff_x = abs(x - other.x);
        float diff_y = abs(y - other.y);
        
        bool close_x = (diff_x < MIN_TOL) || (x != 0.0f && (diff_x / abs(x)) < MIN_TOL);
        bool close_y = (diff_y < MIN_TOL) || (y != 0.0f && (diff_y / abs(y)) < MIN_TOL);
        
        return close_x && close_y;
    }

    XY operator+(const XY& other) const {
        return XY{x + other.x, y + other.y};
    }

    XY& operator+=(const XY& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    XY operator-(const XY& other) const {
        return XY{x - other.x, y - other.y};
    }

    XY operator-=(const XY& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    XY operator*(float s) const {
        return XY{x * s, y * s};
    }

    // friend: defines a free function so float*XY works (member operator only handles XY*float)
    friend XY operator*(float s, const XY& v) {
        return XY{s * v.x, s * v.y};
    }

    void setXY(float new_x, float new_y) { x = new_x; y = new_y; }
    void setXY(XY other) { x = other.x; y = other.y; }

    void translate(float dx, float dy) { x += dx; y += dy; }
    void translate(XY move) { x += move.x; y += move.y; }

    string to_string() {
        std::ostringstream ss;
        ss << std::setprecision(2) << std::fixed;
        ss << "@(" << x << ", " << y << ")";
        return ss.str();
    }

};

typedef XY Position;    // alias for XY
typedef XY Gradient; // alias for XY

struct Term
{
    XY plus;
    XY minus;
    void clear() { plus.clear(); minus.clear(); }
    string to_string()
    {
        std::ostringstream ss;
        ss << "Term(+" << plus.to_string() << ", -" << minus.to_string() << ")";
        return ss.str();
    }
};

// Execution time tracking functions
long getTime();
double getInterval(long start_time, long end_time);

/**
 * @brief Run-wide reduction policy for the threaded iteration (config params.deterministic).
 *
 * The placement iteration is parallelized with OpenMP over nodes, nets and grid rows. `+` on
 * floats is not associative, so a threaded loop is only bit-reproducible if it does not
 * reorder additions. Three kinds of loop, and only the third is governed by this flag:
 *
 *   1. Disjoint writes (node steps, per-row transforms, bin clears) — threaded always. The
 *      per-item arithmetic is untouched and nothing is summed, so the schedule cannot matter.
 *   2. Scalar reductions (L1 norms, total HPWL, total overflow) — threaded always, through
 *      OrderedReduce below, which sums in index order for a few percent of extra memory
 *      traffic. Not worth a policy switch.
 *   3. SCATTER reductions — depositing cell area into shared bins, and accumulating a net's
 *      gradient onto shared nodes. These are the expensive ones and the only ones the flag
 *      governs:
 *        false — one atomic add per deposit. Fastest, but the order in which several threads
 *                hit the same bin or node follows their interleaving, so results move slightly
 *                from run to run even at a fixed thread count.
 *        true  — the per-item work (footprint geometry, the WA exponentials) still runs
 *                threaded, but the shared add is replayed serially in the original item order,
 *                so the result is bit-identical to the serial golden at any thread count.
 *
 * Default true: sw_only is the reference every pl_algo hardware block is verified against, so
 * reproducibility is the property that matters most. Sweeps that only need throughput can set
 * it false. Set once during setup and never written again, so the hot loops can read it freely.
 */
extern bool g_deterministic;

/**
 * @brief A sum whose terms are computed in parallel but ADDED UP in index order.
 *
 * An `omp reduction` gives a different low bit for a different thread count, because each
 * thread folds its own partial and the partials are combined in whatever order they finish.
 * Here the per-item work — which is what costs anything — runs threaded into a scratch array,
 * and the accumulation then runs on one thread in increasing index: the same additions, in the
 * same sequence, as the equivalent single-threaded loop. The scratch is a member so the
 * per-iteration allocation happens once.
 */
class OrderedReduce
{
    std::vector<float> m_partials;

public:
    /// @brief @p per_item(i) -> the i-th term. Returns the terms summed in increasing i.
    template <typename F>
    float sum(int count, F&& per_item)
    {
        if ((int)m_partials.size() < count) m_partials.resize(count);
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < count; i++) m_partials[i] = per_item(i);

        float total = 0.0f;
        for (int i = 0; i < count; i++) total += m_partials[i];
        return total;
    }

    /// @brief Two sums from one pass: @p per_item(i, first_i, second_i) writes both terms.
    ///        Each is accumulated in increasing i, independently of the other.
    template <typename F>
    void sum2(int count, F&& per_item, float& first, float& second)
    {
        if ((int)m_partials.size() < 2 * count) m_partials.resize(2 * count);
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < count; i++) per_item(i, m_partials[2*i], m_partials[2*i + 1]);

        first = 0.0f; second = 0.0f;
        for (int i = 0; i < count; i++) { first += m_partials[2*i]; second += m_partials[2*i + 1]; }
    }
};

AIEPLACE_NAMESPACE_END

