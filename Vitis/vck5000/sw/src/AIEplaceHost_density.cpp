#define _GLIBCXX_USE_CXX11_ABI 0

#include "experimental/xrt_kernel.h"
#include "experimental/xrt_uuid.h"
#include <stdio.h>
#include <sys/time.h>
#include <iostream>
#include <fstream>

using namespace std;

#define DEVICE_ID 0 // terminate called after throwing an instance of 'std::runtime_error'
                    //  what():  Could not open device with index '0'

static long getEpoch();
static double getTiming(long, long);
//static bool check_results(float*, float, int);
static void init_problem(float*, int);

int main(int argc, char * argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Must provide bitstream and design files for AIEplace\n");
    return -1;
  }
  printf("Running AIEplace on VCK5000\n");

  int data_size = stoi(argv[2]); // how much data is being sent?
  float * input_data=new float[data_size]; // 4 extra control floats
  float * result_data=new float[data_size];
  init_problem(input_data, data_size);

  std::string xclbin_file=std::string(argv[1]);
  //std::string input_dir = argv[2];

  //Open Xilinx Device
  std::cout << "Loading xclbin: " << xclbin_file << std::endl;
  xrt::device device = xrt::device(DEVICE_ID);
  std::cout << "Device ID " << DEVICE_ID << " found!" << std::endl;

  // Load xclbin which includes PL and AIE graph
  xrt::uuid xclbin_uuid = device.load_xclbin(xclbin_file);

  std::cout << "Create PL kernels" << std::endl;
  xrt::kernel device_mm2s = xrt::kernel(device, xclbin_uuid, "device_mm2aie2mm");
  //xrt::kernel device_s2mm = xrt::kernel(device, xclbin_uuid, "device_s2mm");

  // Get memory bank groups for device buffers
  xrtMemoryGroup bank_input  = device_mm2s.group_id(0);
  xrtMemoryGroup bank_result = device_mm2s.group_id(1);

  // Create device buffer objects
  std::cout << "Create input and output device buffers" << std::endl;
  xrt::bo input_buffer  = xrt::bo(device, (data_size) * sizeof(float), /*xrt::bo::flags::normal,*/ bank_input);
  xrt::bo result_buffer = xrt::bo(device, data_size * sizeof(float),   /*xrt::bo::flags::normal,*/ bank_result);

  // Create kernel runner instances
  std::cout << "Create runner" << std::endl;
  xrt::run run_device_mm2s(device_mm2s);
  //xrt::run run_device_s2mm(device_s2mm);

  // set kernel arguments
  run_device_mm2s.set_arg(0, input_buffer);
  run_device_mm2s.set_arg(1, result_buffer);
  run_device_mm2s.set_arg(2, data_size);

  //run_device_s2mm.set_arg(0, result_buffer);
  //run_device_s2mm.set_arg(1, data_size);

  std::cout << "Transfer input data to device... ";
  long start=getEpoch();
  input_buffer.write(input_data);
  input_buffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
  double xfer_on_time=getTiming(getEpoch(), start);
  std::cout << "Done" << std::endl;

  std::cout << "Run kernels... " ;
  start=getEpoch();
  run_device_mm2s.start();
  run_device_mm2s.wait();
  //run_device_s2mm.start();
  //run_device_s2mm.wait();
  double kernel_exec_time=getTiming(getEpoch(), start);
  std::cout << "Finish" << std::endl;

  std::cout << "Transfer results to host... ";
  start=getEpoch();
  result_buffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
  result_buffer.read(result_data);
  double xfer_off_time=getTiming(getEpoch(), start);
  std::cout << "Done" << std::endl;

  //bool results_validated=check_results(result_data, add_value, data_size);

  //if (results_validated) {
  //  printf("Results validated OK\n");
  //} else {
  //  printf("Warning - results not validated\n");
  //}

  printf("Total runtime : %f sec, (%f xfer on, %f execute, %f xfer off)\n", xfer_on_time+kernel_exec_time+xfer_off_time, xfer_on_time, kernel_exec_time, xfer_off_time);

  // Can comment in for debugging
  printf("Results:\n");
  for (int i=0;i<data_size;i++) {
    printf("%d: %f\n", i, result_data[i]);
  }
  return 0;
}

static long getEpoch() {
  struct timeval tm;
  gettimeofday(&tm, NULL);
  return (tm.tv_sec * 1000000)+tm.tv_usec;
}

static double getTiming(long end_time, long start_time) {
  return (end_time - start_time) /1.0e6 ;
}

// Read input data from file
static void init_problem(float * input_data, int data_size) {
  ifstream in_file;
  in_file.open("../aie/golden_data/density/input.dat");
  string line;
  int i = 0;
  if(in_file.is_open())
  {
    while( getline (in_file, line) )
    {
      cout << "line " << i << ": " << line << endl;
      input_data[i++]=stoi(line);
    }
    in_file.close();
  } else printf("Unable to open input file!\n");
}

//static bool check_results(float * results, float add_value, int data_size) {
//  int i;
//  for (i=0;i<data_size;i++) {
//    if (results[i] != i+add_value) return false;
//  }
//  return true;
//}

//static void init_problem(float * input_data, int data_size) {
//  int i;
//  for (i=0;i<data_size;i++) {
//    input_data[i]=i;
//   }
//}
