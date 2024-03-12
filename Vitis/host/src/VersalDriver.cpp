#include "VersalDriver.h"

AIEPLACE_NAMESPACE_BEGIN

void VersalDriver::VersalDriver(std::string xclbin_file)
{
    load_xclbin(xclbin_file);
    create_PL_kernels();
}

void VersalDriver::load_xclbin(std::string xclbin_file)
{
    //Open Xilinx Device
    if(print) std::cout << "Loading xclbin: " << xclbin_file << std::endl;
    xrt::device device = xrt::device(DEVICE_ID);
    if(print) std::cout << "Device ID " << DEVICE_ID << " found!" << std::endl;
    
    // Load xclbin which includes PL and AIE graph
    xrt::uuid xclbin_uuid = device.load_xclbin(xclbin_file);
}

void VersalDriver::create_PL_kernels()
{
    if(print) std::cout << "Create PL kernels" << std::endl;
    m_device_mm2s = xrt::kernel(device, xclbin_uuid, "device_mm2s");
    m_device_s2mm = xrt::kernel(device, xclbin_uuid, "device_s2mm");

    // Get memory bank groups for device buffers
    m_bank_input  = m_device_mm2s.group_id(0);
    m_bank_result = m_device_s2mm.group_id(0);

    // Create device buffer objects
    if(print) std::cout << "Create input and output device buffers" << std::endl;
    m_input_buffer  = xrt::bo(device, (DATA_XFER_SIZE + 4) * sizeof(float), /*xrt::bo::flags::normal,*/ m_bank_input);
    m_result_buffer = xrt::bo(device, DATA_XFER_SIZE * sizeof(float),   /*xrt::bo::flags::normal,*/ m_bank_result);
    if(print) std::cout << "Buffer size: " << DATA_XFER_SIZE * sizeof(float) << std::endl;

    // Create kernel runner instances
    if(print) std::cout << "Create runner" << std::endl;
    m_run_device_mm2s = xrt::run(device_mm2s);
    m_run_device_s2mm = xrt::run(device_s2mm);

    // set kernel arguments
    m_run_device_mm2s.set_arg(0, m_input_buffer);
    m_run_device_mm2s.set_arg(1, DATA_XFER_SIZE );

    m_run_device_s2mm.set_arg(0, m_result_buffer);
    m_run_device_s2mm.set_arg(1, DATA_XFER_SIZE );
}

void VersalDriver::run_PL_kernels(float * input_data, float * result_data)
{
    if(print) std::cout << "Transfer input data to device... ";
    long start=getEpoch();
    m_input_buffer.write(input_data);
    m_input_buffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    double xfer_on_time=getTiming(getEpoch(), start);
    if(print) std::cout << "Done" << std::endl;

    if(print) std::cout << "Run kernels... " ;
    start=getEpoch();
    m_run_device_mm2s.start();
    m_run_device_mm2s.wait();
    m_run_device_s2mm.start();
    m_run_device_s2mm.wait();
    double kernel_exec_time=getTiming(getEpoch(), start);
    if(print) std::cout << "Finish" << std::endl;

    if(print) std::cout << "Transfer results to host... ";
    start=getEpoch();
    m_result_buffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    m_result_buffer.read(result_data);
    double xfer_off_time=getTiming(getEpoch(), start);
    if(print) std::cout << "Done!" << std::endl;
}

void VersalDriver::print_runtime()
{
    printf("Total runtime : %f sec, (%f xfer on, %f execute, %f xfer off)\n", xfer_on_time+kernel_exec_time+xfer_off_time, xfer_on_time, kernel_exec_time, xfer_off_time);

    // Can comment in for debugging
    printf("Results:\n");
    for (int i=0;i<data_size;i++) {
        printf("Output %d: %f\n", i, result_data[i]);
    }

}

static long VersalDriver::getEpoch() {
  struct timeval tm;
  gettimeofday(&tm, NULL);
  return (tm.tv_sec * 1000000)+tm.tv_usec;
}

static double VersalDriver::getTiming(long end_time, long start_time) {
  return (end_time - start_time) / 1.0e6 ;
}

AIEPLACE_NAMESPACE_END