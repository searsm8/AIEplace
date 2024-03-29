#include "VersalDriver.h"

AIEPLACE_NAMESPACE_BEGIN

// Constructor
VersalDriver::VersalDriver(std::string xclbin_file, int device_id, int num_pipelines) :
    m_device_id(device_id), m_num_pipelines(num_pipelines)
{
    m_kernel = new VersalKernel[m_num_pipelines];
    //Open Xilinx Device
    if(print) std::cout << "Loading xclbin: " << xclbin_file << std::endl;
    m_device = xrt::device(m_device_id);
    if(print) std::cout << "Device ID " << m_device_id << " found!" << std::endl;
    
    // Load xclbin which includes PL and AIE graph
    if(print) std::cout << "Loading XCL bin..." << endl;
    xrt::uuid xclbin_uuid = m_device.load_xclbin(xclbin_file);

    if(print) std::cout << "Create PL kernels" << std::endl;
    for(int i = 0; i < m_num_pipelines; i++) {
        if(print) std::cout << "Create kernel " << i << std::endl;
        m_kernel[i].device_mm2s = xrt::kernel(m_device, xclbin_uuid, "device_mm2s");
        m_kernel[i].device_s2mm = xrt::kernel(m_device, xclbin_uuid, "device_s2mm");

        // Get memory bank groups for device buffers
        m_kernel[i].bank_input  = m_kernel[i].device_mm2s.group_id(0);
        m_kernel[i].bank_result = m_kernel[i].device_s2mm.group_id(0);

        // Create device buffer objects
        m_kernel[i].input_buffer  = xrt::bo(m_device, (DATA_XFER_SIZE + 4) * sizeof(float), /*xrt::bo::flags::normal,*/ m_kernel[i].bank_input);
        m_kernel[i].result_buffer = xrt::bo(m_device, DATA_XFER_SIZE * sizeof(float),   /*xrt::bo::flags::normal,*/ m_kernel[i].bank_result);

        // Create kernel runner instances
        m_kernel[i].run_device_mm2s = xrt::run(m_kernel[i].device_mm2s);
        m_kernel[i].run_device_s2mm = xrt::run(m_kernel[i].device_s2mm);

        // set kernel arguments
        m_kernel[i].run_device_mm2s.set_arg(0, m_kernel[i].input_buffer);
        m_kernel[i].run_device_mm2s.set_arg(1, DATA_XFER_SIZE + 4);

        m_kernel[i].run_device_s2mm.set_arg(0, m_kernel[i].result_buffer);
        m_kernel[i].run_device_s2mm.set_arg(1, DATA_XFER_SIZE );
    }
}

void VersalDriver::start_PL_kernel(int kernel_index, float * input_data, int input_size)
{
    cout << "Versal Driver starting kernel " << kernel_index << endl;
    //if(print) std::cout << "Transfer input data to device... ";
    m_kernel[kernel_index].start_time = getEpoch();
    m_kernel[kernel_index].input_buffer.write(input_data);
    m_kernel[kernel_index].input_buffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    m_kernel[kernel_index].xfer_on_time = getTiming(getEpoch(), m_kernel[kernel_index].start_time);
    //if(print) std::cout << "Done" << std::endl;

    //if(print) std::cout << "Run kernels... " ;
    m_kernel[kernel_index].start_time = getEpoch();
    m_kernel[kernel_index].run_device_mm2s.start();
    m_kernel[kernel_index].run_device_mm2s.wait();
    m_kernel[kernel_index].run_device_s2mm.start();
}

float VersalDriver::wait_for_finish(int kernel_index, float * output_data, int output_size)
{
    m_kernel[kernel_index].run_device_s2mm.wait();
    m_kernel[kernel_index].kernel_exec_time = getTiming(getEpoch(), m_kernel[kernel_index].start_time);
    if(print) std::cout << "Finish" << std::endl;

    if(print) std::cout << "Transfer results to host... ";
    m_kernel[kernel_index].start_time = getEpoch();
    m_kernel[kernel_index].result_buffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    //m_kernel[kernel_index].result_buffer.read(m_kernel[kernel_index].output_data); // TODO use output_data from VersalKernel
    m_kernel[kernel_index].result_buffer.read(output_data);
    m_kernel[kernel_index].xfer_off_time=getTiming(getEpoch(), m_kernel[kernel_index].start_time);
    if(print) std::cout << "Done!" << std::endl;
}

void VersalDriver::print_results()
{
    for(int i = 0; i < m_num_pipelines; i++) {
        printf("Kernel %d Total runtime : %f sec, (%f xfer on, %f execute, %f xfer off)\n", i,
            m_kernel[i].xfer_on_time + m_kernel[i].kernel_exec_time + m_kernel[i].xfer_off_time,
            m_kernel[i].xfer_on_time,
            m_kernel[i].kernel_exec_time,
            m_kernel[i].xfer_off_time);

        //// Debugging
        //printf("Results:\n");
        //for (int i=0; i < DATA_XFER_SIZE; i++) {
        //    printf("Output %d: %f\n", i, result_data[i]);
    }

}

long VersalDriver::getEpoch() {
  struct timeval tm;
  gettimeofday(&tm, NULL);
  return (tm.tv_sec * 1000000)+tm.tv_usec;
}

double VersalDriver::getTiming(long end_time, long start_time) {
  return (end_time - start_time) / 1.0e6 ;
}

AIEPLACE_NAMESPACE_END