#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>
#include <cassert>
#include <math.h>

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::map;
using std::max;
using std::min;

// Compilation flags
#define USE_AIE_ACCELERATION
#define CREATE_VISUALIZATION
#define PACKET_SIZE 8 // number of floats transferred to AIE kernels at a time. DO NOT TOUCH

// PARTIALS_GRAPH_COUNT is the number of compute units on AIE for partials acceleration.
// This is used when building the AIE graphs, and determines how many MM2S and S2MM data movers are required
// and of course it is also used by the host code.
// Therefore, changing this value requires a complete rebuild of the entire project
#define PARTIALS_GRAPH_COUNT 4

// ePlace hyperparameters
#define INITIAL_LAMBDA 0.002
#define WIRELENGTH_COMPUTE_METHOD "HPWL"

// other parameters
#define BINS_PER_ROW 128 // Should be scaled up to 512 or 1024 for final application
#define BINS_PER_COL 128



#define AIEPLACE_NAMESPACE_BEGIN namespace AIEplace {
#define AIEPLACE_NAMESPACE_END }

AIEPLACE_NAMESPACE_BEGIN 

typedef float position_type;

struct XY
{
    double x;
    double y;
    void clear() { x = 0; y = 0;}
};

AIEPLACE_NAMESPACE_END 

#endif