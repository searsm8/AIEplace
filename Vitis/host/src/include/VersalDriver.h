#ifndef VERSALDRIVER_H
#define VERSALDRIVER_H

#define _GLIBCXX_USE_CXX11_ABI 0
#include "Common.h"
#include "experimental/xrt_kernel.h"
#include "experimental/xrt_uuid.h"
#include <sys/time.h>

AIEPLACE_NAMESPACE_BEGIN
using namespace xrt;

class VersalDriver
{
public:
    bool print = true;
    uuid m_xclbin_uuid;

    kernel m_device_mm2s;
    kernel m_device_s2mm;

    xrtMemoryGroup m_bank_input;
    xrtMemoryGroup m_bank_result;
    // buffer objects which hold inputs and outputs
    bo m_input_buffer;
    bo m_result_buffer;

    run m_run_device_mm2s;
    run m_run_device_s2mm;
    
    
    // Constructors
    VersalDriver(std::string xclbin_file);

    void load_xclbin(std::string xclbin_file);

    void create_PL_kernels();
    void run_PL_kernels(float * input_data, float * result_data);
    
    void print_runtime();

    static long getEpoch();
    static double getTiming(long end_time, long start_time);
};

AIEPLACE_NAMESPACE_END

#endif