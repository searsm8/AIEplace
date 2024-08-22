#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <iomanip>
#include <fstream>
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

// Sizes used in multiple files
#define TEST_NET_SIZE 6 // for testing and debugging only
#define VEC_SIZE 8 // DO NOT TOUCH. This is the number of floats transferred to AIE kernels at a time.
#define NETS_PER_GROUP 4 // each AIE vector is of size 8, and we process X and Y data for the net, so 4 nets fit into a vector
#define LCM_BUFFSIZE 840 // Set buffer size to 840 (need extra 8 for control data)
                        //  840 is the Least Common Multiple (LCM) of netsizes 2 thru 8
#define INPUT_PACKET_SIZE VEC_SIZE*(LCM_BUFFSIZE+1)
#define OUTPUT_PACKET_SIZE VEC_SIZE*LCM_BUFFSIZE

// PARTIALS_GRAPH_COUNT is the number of compute units on AIE for partials acceleration.
// This is used when building the AIE graphs, and determines how many MM2S and S2MM data movers are required
// and it is also used by the host code to know how many compute units to send data to.
// Therefore, changing this value requires a complete rebuild of the entire project
#define PARTIALS_GRAPH_COUNT 8

// ePlace hyperparameters
#define INITIAL_LAMBDA .02 // inital lambda for each bin
#define WIRELENGTH_COMPUTE_METHOD "HPWL"

// granularity of bin grid
#define BINS_PER_ROW 32 // Should be scaled up to 512 or 1024 for final application
#define BINS_PER_COL BINS_PER_ROW // Unless otherwise noted, grid of bins is square.

#define AIEPLACE_NAMESPACE_BEGIN namespace AIEplace {
#define AIEPLACE_NAMESPACE_END }

#define AIEPLACE_VERSION "v0.0.1"
#define stringify std::to_string

AIEPLACE_NAMESPACE_BEGIN 

typedef float position_type;

// Contains XY data, which might be coordinates or any other pair of data.
struct XY
{
    float x;
    float y;
    void clear() { x = 0; y = 0;}

    #define MIN_TOL 0.01
    bool isClose(XY other)
    {
        bool close = true;
        float diff_x = abs(x - other.x);
        if(!((diff_x < MIN_TOL) || (diff_x / x < MIN_TOL)))
            close = false;
        float diff_y = abs(y - other.y);
        if(!((diff_y < MIN_TOL) || (diff_y / y < MIN_TOL)))
            close = false;
        return close;
    }
};

AIEPLACE_NAMESPACE_END 

#endif