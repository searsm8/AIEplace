#include "GraphDriver.h"

AIEPLACE_NAMESPACE_BEGIN

void GraphDriver::print_info()
{
    printf("Kernel Total runtime : %f sec, (%f xfer on, %f execute, %f xfer off)\n",
        xfer_on_time + kernel_exec_time + xfer_off_time,
        xfer_on_time,
        kernel_exec_time,
        xfer_off_time);
}

/*
 * Send packets of data to the PL data mover kernels.
 * Each packet consists of 8 floats, one coordinate from each net.
 * 
 * @param: input_start is an array of floats of size 8*net_size
 */
void GraphDriver::send_packet(float * packet)
{
    start_time = getEpoch();
    input_buffer.write(packet);
    input_buffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    xfer_on_time = getTiming(getEpoch(), start_time);
    start_time = getEpoch();

    //DEBUG
    //SHOULD USE LOGGER log_debug(msg)
    //print input_data
    //cout << "send_packet input -- kernel_id = " << m_kernel_id;
    //for(int i = 0; i < INPUT_PACKET_SIZE; i++){
    //    //if(packet[i] == 0) continue;
    //    if(i%8 == 0) cout << endl;
    //    cout << packet[i] << " ";
    //}
    //cout << endl;

    // Start data movers in PL
    run_device_mm2s.start();
    run_device_s2mm.start();
}

float GraphDriver::receive_packet(float * output_data)
{
    // wait for data movers to finish
    run_device_mm2s.wait();
    Logger::log_trace("run_device_mm2s.wait();");
    run_device_s2mm.wait(); // gets stuck here!
    Logger::log_trace("run_device_s2mm.wait();");
    kernel_exec_time = getTiming(getEpoch(), start_time);
    start_time = getEpoch();
    result_buffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    result_buffer.read(output_data);

    //DEBUG
    //SHOULD USE LOGGER log_debug(msg)
    //print output_data
    //cout << "receive_packet output -- kernel_id = " << m_kernel_id;
    //for(int i = 0; i < OUTPUT_PACKET_SIZE; i++){
    //    //if(output_data[i] == 0) continue;
    //    if(i%8 == 0) cout << endl;
    //    cout << output_data[i] << " ";
    //}
    //cout << endl;

    xfer_off_time=getTiming(getEpoch(), start_time);
}

void PartialsGraphDriver::init(xrt::device device, xrt::uuid & xclbin_uuid, int kernel_id)
{
    Logger::log_trace("PartialsGraphDriver creating PL kernel. Kernel ID: " + std::to_string(kernel_id));
    m_kernel_id = kernel_id;
    
    // Create kernel objects
    // Be extra sure the names are correct, there might not be an error message!
    device_mm2s = xrt::kernel(device, xclbin_uuid, "partials_mm2s:{partials_mm2s_"+std::to_string(kernel_id)+"}");
    device_s2mm = xrt::kernel(device, xclbin_uuid, "partials_s2mm:{partials_s2mm_"+std::to_string(kernel_id)+"}");

    // Get memory bank groups for device buffers
    bank_input  = device_mm2s.group_id(0);
    bank_result = device_s2mm.group_id(0);


    // Create kernel runner instances
    run_device_mm2s = xrt::run(device_mm2s);
    run_device_s2mm = xrt::run(device_s2mm);

    input_buffer  = xrt::bo(device, INPUT_PACKET_SIZE*sizeof(float), /*xrt::bo::flags::normal,*/ device_mm2s.group_id(0));
    result_buffer = xrt::bo(device, OUTPUT_PACKET_SIZE*sizeof(float), /*xrt::bo::flags::normal,*/ device_s2mm.group_id(0));

    // set kernel arguments
    run_device_mm2s.set_arg(0, input_buffer);
    run_device_mm2s.set_arg(1, INPUT_PACKET_SIZE); // extra size for ctrl data

    run_device_s2mm.set_arg(0, result_buffer);
    run_device_s2mm.set_arg(1, OUTPUT_PACKET_SIZE);
}


void DensityGraphDriver::init(xrt::device device, xrt::uuid & xclbin_uuid, int kernel_id, int bins_per_row)
{
    Logger::log_trace("DensityGraphDriver creating PL kernel. Kernel ID: " + std::to_string(kernel_id));
    m_kernel_id = kernel_id;
    // kernel_id = 0 -> DCT
    // kernel_id = 1 -> IDCT
    // kernel_id = 2 -> IDXST
    
    // Create kernel objects
    // Be extra sure the names are correct, there might not be an error message!
    device_mm2s = xrt::kernel(device, xclbin_uuid, "density_mm2s:{density_mm2s_"+std::to_string(kernel_id)+"}");
    device_s2mm = xrt::kernel(device, xclbin_uuid, "density_s2mm:{density_s2mm_"+std::to_string(kernel_id)+"}");

    // Get memory bank groups for device buffers
    bank_input  = device_mm2s.group_id(0);
    bank_result = device_s2mm.group_id(0);


    // Create kernel runner instances
    run_device_mm2s = xrt::run(device_mm2s);
    run_device_s2mm = xrt::run(device_s2mm);

    // For density DCT computation, we transfer one row's worth of floats to compute DCT
    input_buffer  = xrt::bo(device, 2*bins_per_row*sizeof(float), /*xrt::bo::flags::normal,*/ device_mm2s.group_id(0));
    result_buffer = xrt::bo(device, 2*bins_per_row*sizeof(float), /*xrt::bo::flags::normal,*/ device_s2mm.group_id(0));

    // set kernel arguments
    run_device_mm2s.set_arg(0, input_buffer);
    run_device_mm2s.set_arg(1, 2*bins_per_row);

    run_device_s2mm.set_arg(0, result_buffer);
    run_device_s2mm.set_arg(1, 2*bins_per_row);
}

AIEPLACE_NAMESPACE_END