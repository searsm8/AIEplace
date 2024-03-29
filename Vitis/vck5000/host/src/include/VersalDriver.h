#ifndef VERSALDRIVER_H
#define VERSALDRIVER_H

#define _GLIBCXX_USE_CXX11_ABI 0
#define DEVICE_ID 0

#include "Common.h"
#include "experimental/xrt_kernel.h"
#include "experimental/xrt_uuid.h"
#include <sys/time.h>

AIEPLACE_NAMESPACE_BEGIN

class VersalDriver
{
public:
    struct VersalKernel
    {
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
    };

    // Member data
    bool print = true;

    xrt::device m_device;
    //xrt::uuid m_xclbin_uuid; // making this a member data causes compile errors for unknown reason

    VersalKernel* m_kernel;
    int m_num_pipelines, m_device_id;
    
    // Constructor
    VersalDriver(std::string xclbin_file, int device_id, int num_pipelines);

    void start_PL_kernel(int kernel_index, float *input_data, int input_size);
    float wait_for_finish(int kernel_index, float * output_data, int output_size);

    void print_results();

    long getEpoch();
    double getTiming(long end_time, long start_time);
    VersalKernel getKernel(int index) { return m_kernel[index]; }
};

AIEPLACE_NAMESPACE_END

#endif