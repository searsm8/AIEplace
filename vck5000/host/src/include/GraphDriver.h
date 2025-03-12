#ifndef GRAPHDRIVER_H
#define GRAPHDRIVER_H

#define _GLIBCXX_USE_CXX11_ABI 0

#include "Common.h"
#include "experimental/xrt_kernel.h"
#include "experimental/xrt_aie.h" // for Graph APIs
#include "experimental/xrt_uuid.h"
#include <sys/time.h>
#include "Logger.h"

AIEPLACE_NAMESPACE_BEGIN

// used for timing performance in GraphDrivers and AIEplace algorithm execution
class Timer {
  std::chrono::high_resolution_clock::time_point mTimeStart;
public:
  Timer() { reset(); }
  long long stop() {
    std::chrono::high_resolution_clock::time_point timeEnd =
        std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>
                    (timeEnd - mTimeStart).count();
  }
  void reset() { mTimeStart = std::chrono::high_resolution_clock::now(); }
};


// Base class
class GraphDriver
{
public:
    // Member data
    long start_time, xfer_on_time, xfer_off_time, kernel_exec_time;
    xrt::kernel device_mm2s;
    xrt::kernel device_s2mm;

    xrtMemoryGroup bank_input;
    xrtMemoryGroup bank_result;

    // buffer objects to hold inputs and outputs
    xrt::bo input_buffer;
    xrt::bo result_buffer;

    xrt::run run_device_mm2s;
    xrt::run run_device_s2mm;

    //xrt::uuid m_xclbin_uuid;
    // making this a member data causes compile errors for unknown reason
    // instead, this object is only used in init()

    GraphDriver() {}
    void init(xrt::device device, xrt::uuid & xclbin_uuid);
    void setBufferSize(int size);

    void send_packet(float * input_data);
    float receive_packet(float * output_data);

    void print_info();
};

class PartialsGraphDriver : public GraphDriver
{
public:
    PartialsGraphDriver() {}
    void init(xrt::device device, xrt::uuid & xclbin_uuid, int kernel_id);
};


class DensityGraphDriver : public GraphDriver
{
public:
    DensityGraphDriver() {}
    void init(xrt::device device, xrt::uuid & xclbin_uuid, int kernel_id, int bins_per_row);
};

AIEPLACE_NAMESPACE_END

#endif