#define _GLIBCXX_USE_CXX11_ABI 0

#include "experimental/xrt_kernel.h"
#include "experimental/xrt_uuid.h"
#include <stdio.h>
#include <sys/time.h>

#define DEVICE_ID 0 // terminate called after throwing an instance of 'std::runtime_error'
                    //  what():  Could not open device with index '0'

static long getEpoch();
static double getTiming(long, long);
//static bool check_results(float*, float, int);
//static void init_problem(float*, int);

int main(int argc, char * argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Must provide bitstream and design files for AIEplace\n");
    return -1;
  }
  printf("Running AIEplace on VCK5000\n");

  std::string xclbin_file=std::string(argv[1]);
  std::string input_dir = argv[2];

  std::cout << "Loading xclbin: " << xclbin_file << std::endl;
  xrt::device device = xrt::device(DEVICE_ID);
  std::cout << "Device ID " << DEVICE_ID << " found!" << std::endl;
  // Load xclbin which includes PL and AIE graph
  xrt::uuid xclbin_uuid = device.load_xclbin(xclbin_file);

  std::cout << "Create PL kernels" << std::endl;
  xrt::kernel device_mm2s = xrt::kernel(device, xclbin_uuid, "device_mm2s");
  xrt::kernel device_s2mm = xrt::kernel(device, xclbin_uuid, "device_s2mm");

  // get memory bank groups for device buffers
  xrtMemoryGroup bank_input  = device_mm2s.group_id(0);
  xrtMemoryGroup bank_result = device_s2mm.group_id(0);

  // create device buffer objects
  std::cout << "Create input and output device buffers" << std::endl;
  xrt::bo input_buffer  = xrt::bo(device, data_size * sizeof(float), xrt::bo::flags::normal, bank_input);
  xrt::bo result_buffer = xrt::bo(device, data_size * sizeof(float), xrt::bo::flags::normal, bank_result);

  // create kernel runner instances
  std::cout << "Create runner" << std::endl;
  xrt::run run_device_mm2s(device_mm2s);
  xrt::run run_device_s2mm(device_s2mm);

  // set kernel arguments
  run_device_mm2s.set_arg(0, input_buffer);
  run_device_mm2s.set_arg(1, data_size);
  run_device_mm2s.set_arg(2, add_value);

  run_device_s2mm.set_arg(0, result_buffer);
  run_device_s2mm.set_arg(1, data_size);

  std::cout << "Transfer input data to device... ";
  long start=getEpoch();
  input_buffer.write(input_data);
  input_buffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
  double xfer_on_time=getTiming(getEpoch(), start);
  std::cout << "Done" << std::endl;

  std::cout << "Run kernels... " ;
  start=getEpoch();
  run_device_mm2s.start();
  run_device_s2mm.start();
  run_device_s2mm.wait();
  run_device_mm2s.wait();
  double kernel_exec_time=getTiming(getEpoch(), start);
  std::cout << "Finish" << std::endl;

  std::cout << "Transfer results to host... ";
  start=getEpoch();
  result_buffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
  result_buffer.read(result_data);
  double xfer_off_time=getTiming(getEpoch(), start);
  std::cout << "Done" << std::endl;

  //bool results_validated=check_results(result_data, add_value, data_size);

  if (results_validated) {
    printf("Results validated OK\n");
  } else {
    printf("Warning - results not validated\n");
  }

  printf("Total runtime : %f sec, (%f xfer on, %f execute, %f xfer off)\n", xfer_on_time+kernel_exec_time+xfer_off_time, xfer_on_time, kernel_exec_time, xfer_off_time);

  // Can comment in for debugging
  /*
  for (int i=0;i<data_size;i++) {
    printf("%d: %f\n", i, result_data[i]);
  }
  */
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
