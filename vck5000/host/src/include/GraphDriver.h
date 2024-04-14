#ifndef GRAPHDRIVER_H
#define GRAPHDRIVER_H

#define _GLIBCXX_USE_CXX11_ABI 0

#include "Common.h"
#include "experimental/xrt_kernel.h"
#include "experimental/xrt_aie.h" // for Graph APIs
#include "experimental/xrt_uuid.h"
#include <sys/time.h>

AIEPLACE_NAMESPACE_BEGIN

class PartialsGraphDriver
{
public:
    // Member data
    long start_time, xfer_on_time, xfer_off_time, kernel_exec_time;
    xrt::kernel device_mm2s;
    xrt::kernel device_s2mm;

    xrtMemoryGroup bank_input;
    xrtMemoryGroup bank_result;

    // buffer objects which hold inputs and outputs
    xrt::bo input_buffer;
    xrt::bo result_buffer;

    xrt::run run_device_mm2s;
    xrt::run run_device_s2mm;
    float * input_data, output_data;

    // Debugging flag
    bool print = true;

    //xrt::uuid m_xclbin_uuid; // making this a member data causes compile errors for unknown reason

    // Constructor
    PartialsGraphDriver();
    void init(xrt::device device, xrt::uuid & xclbin_uuid, int kernel_id);

    void start(float *input_data);
    float wait(float * output_data);

    void print_info();

    long getEpoch();
    double getTiming(long end_time, long start_time);
};

AIEPLACE_NAMESPACE_END

#endif