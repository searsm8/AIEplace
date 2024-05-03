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
#define USE_AIE_ACCELERATION // if this is defined, the application will use VCK5000 acceleration
                    // (To configure AIE and PL, the .xclbin must be specified as a command line parameter)

#define CREATE_VISUALIZATION // if this is defined, a Visualizer class using Cairo will export images

#define PACKET_SIZE 8 // DO NOT TOUCH. This is the number of floats transferred to AIE kernels at a time.

// PARTIALS_GRAPH_COUNT is the number of compute units on AIE for partials acceleration.
// This is used when building the AIE graphs, and determines how many MM2S and S2MM data movers are required
// and of course it is also used by the host code.
// Therefore, changing this value requires a complete rebuild of the entire project
#define PARTIALS_GRAPH_COUNT 4

// ePlace hyperparameters
#define INITIAL_LAMBDA 0.002
#define WIRELENGTH_COMPUTE_METHOD "HPWL"

// other parameters
#define BINS_PER_ROW 32 // Should be scaled up to 512 or 1024 for final application
#define BINS_PER_COL BINS_PER_ROW // Unless otherwise noted, grid of bins is square.

#define AIEPLACE_NAMESPACE_BEGIN namespace AIEplace {
#define AIEPLACE_NAMESPACE_END }

AIEPLACE_NAMESPACE_BEGIN 

typedef float position_type;

struct XY
{
    float x;
    float y;
    void clear() { x = 0; y = 0;}
};

AIEPLACE_NAMESPACE_END 

#endif