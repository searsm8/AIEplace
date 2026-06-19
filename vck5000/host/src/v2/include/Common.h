#ifndef COMMON_H
#define COMMON_H

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


// Compilation flags
//#define USE_AIE_ACCELERATION // if this is defined, the application will use VCK5000 acceleration
                    // (To configure AIE and PL, the .xclbin must be specified as a command line parameter)

// Sizes used in multiple files
constexpr int TEST_NET_SIZE = 6; // for testing and debugging only
constexpr int VEC_SIZE = 8; // DO NOT TOUCH. This is the number of floats transferred to AIE kernels at a time.
constexpr int NETS_PER_GROUP = 4; // each AIE vector is of size 8, and we process X and Y data for the net, so 4 nets fit into a vector
constexpr int LCM_BUFFSIZE = 840; // Set buffer size to 840 
                        //  840 is the Least Common Multiple (LCM) of netsizes 2 thru 8
constexpr int INPUT_PACKET_SIZE  = VEC_SIZE*(LCM_BUFFSIZE+1); // need extra 8 for control data
constexpr int OUTPUT_PACKET_SIZE = VEC_SIZE*LCM_BUFFSIZE;

// PARTIALS_GRAPH_COUNT is the number of compute units on AIE for partials acceleration.
// This is used when building the AIE graphs, and determines how many MM2S and S2MM data movers are required
// and it is also used by the host code to know how many compute units to send data to.
// Therefore, changing this value requires a complete rebuild of the entire project
constexpr int PARTIALS_GRAPH_COUNT = 2;// 30 or more will require more than 64 compute units in PL
// TODO: rewrite PL kernels to require fewer PL resources!

// ePlace hyperparameters
constexpr int INITIAL_LOCAL_DENSITY_WEIGHT = 1; // initial local density weight for each bin
//#define WIRELENGTH_COMPUTE_METHOD "HPWL"

// granularity of bin grid
#define BINS_PER_ROW 64 //32 // Should be scaled up to 512 or 1024 for final application
#define BINS_PER_COL BINS_PER_ROW // Unless otherwise noted, grid of bins is square.

#define AIEPLACE_NAMESPACE AIEplace

#define AIEPLACE_VERSION "v0.0.2"
#define stringify std::to_string // alias

namespace AIEPLACE_NAMESPACE {

  typedef float position_type;

  // Execution time tracking functions
  long getTime();
  double getInterval(long start_time, long end_time);

  std::size_t get_index(const std::thread::id id);

}

#endif
