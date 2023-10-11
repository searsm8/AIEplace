#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>
#include <filesystem>
#include <cassert>
#include <math.h>

namespace fs = std::filesystem;

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
//#define USE_AIE_ACCELERATION
//#define CREATE_VISUALIZATION

#define BINS_PER_ROW 128
#define BINS_PER_COL 128
#define INITIAL_LAMBDA 0.002

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