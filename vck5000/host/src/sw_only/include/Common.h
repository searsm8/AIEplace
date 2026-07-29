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
#include <thread>
#include <mutex>

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

std::size_t get_index(const std::thread::id id);

AIEPLACE_NAMESPACE_END 

