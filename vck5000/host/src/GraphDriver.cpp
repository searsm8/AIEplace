#include "GraphDriver.h"

AIEPLACE_NAMESPACE_BEGIN

// Constructor
PartialsGraphDriver::PartialsGraphDriver() {}

void PartialsGraphDriver::init(xrt::device device, xrt::uuid & xclbin_uuid, int kernel_id)
{
    if(print) std::cout << "PartialsGraphDriver creating PL kernels..." << std::endl;
    
    // Create kernel objects
    // Be extra sure the names are correct, there might not be an error message!
    device_mm2s = xrt::kernel(device, xclbin_uuid, "pl_kernel_mm2s:{pl_kernel_mm2s_"+std::to_string(kernel_id)+"}");
    device_s2mm = xrt::kernel(device, xclbin_uuid, "pl_kernel_s2mm:{pl_kernel_s2mm_"+std::to_string(kernel_id)+"}");

    // Get memory bank groups for device buffers
    bank_input  = device_mm2s.group_id(0);
    bank_result = device_s2mm.group_id(0);

    // Create device buffer objects
    input_buffer  = xrt::bo(device, (DATA_XFER_SIZE + 4) * sizeof(float), /*xrt::bo::flags::normal,*/ bank_input);
    result_buffer = xrt::bo(device, DATA_XFER_SIZE * sizeof(float),   /*xrt::bo::flags::normal,*/ bank_result);

    // Create kernel runner instances
    run_device_mm2s = xrt::run(device_mm2s);
    run_device_s2mm = xrt::run(device_s2mm);

    // set kernel arguments
    run_device_mm2s.set_arg(0, input_buffer);
    run_device_mm2s.set_arg(1, DATA_XFER_SIZE + 4);

    run_device_s2mm.set_arg(0, result_buffer);
    run_device_s2mm.set_arg(1, DATA_XFER_SIZE );
}

void PartialsGraphDriver::send_input(float * input_data)
{
    cout << "Versal Driver starting kernel." << endl;
    //if(print) std::cout << "Transfer input data to device... ";
    start_time = getEpoch();
    input_buffer.write(input_data);
    input_buffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    xfer_on_time = getTiming(getEpoch(), start_time);
    //if(print) std::cout << "Done" << std::endl;

    //if(print) std::cout << "Run kernels... " ;
    start_time = getEpoch();
    run_device_mm2s.start();
    run_device_mm2s.wait();
    run_device_s2mm.start();
}

float PartialsGraphDriver::receive_output(float * output_data)
{
    run_device_s2mm.wait();
    kernel_exec_time = getTiming(getEpoch(), start_time);
    if(print) std::cout << "Finished running graph." << std::endl;

    if(print) std::cout << "Transfer results to host... ";
    start_time = getEpoch();
    result_buffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    //result_buffer.read(output_data); // TODO use output_data from VersalGraph
    result_buffer.read(output_data);
    xfer_off_time=getTiming(getEpoch(), start_time);
    if(print) std::cout << "Done!" << std::endl;
}

void PartialsGraphDriver::print_info()
{
    printf("Kernel Total runtime : %f sec, (%f xfer on, %f execute, %f xfer off)\n",
        xfer_on_time + kernel_exec_time + xfer_off_time,
        xfer_on_time,
        kernel_exec_time,
        xfer_off_time);

    //// Debugging
    //printf("Results:\n");
    //for (int i=0; i < DATA_XFER_SIZE; i++) {
    //    printf("Output %d: %f\n", i, result_data[i]);
}

long PartialsGraphDriver::getEpoch() {
  struct timeval tm;
  gettimeofday(&tm, NULL);
  return (tm.tv_sec * 1000000)+tm.tv_usec;
}

double PartialsGraphDriver::getTiming(long end_time, long start_time) {
  return (end_time - start_time) / 1.0e6 ;
}

AIEPLACE_NAMESPACE_END