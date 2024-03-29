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

#define AIEPLACE_NAMESPACE_BEGIN namespace AIEplace {
#define AIEPLACE_NAMESPACE_END }

#define WIRELENGTH_COMPUTE_METHOD "HPWL"
//#define USE_AIE_ACCELERATION // set in Makefile
//#define CREATE_VISUALIZATION // set in Makefile

#define BINS_PER_ROW 128 // Should be scaled up to 512 or 1024 for final application
#define BINS_PER_COL 128
#define INITIAL_LAMBDA 0.002

#define DATA_XFER_SIZE 8*8 // number of floats transferred to AIE kernels per run
#define NUM_PIPELINES 2

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