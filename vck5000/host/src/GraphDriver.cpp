#include "GraphDriver.h"

AIEPLACE_NAMESPACE_BEGIN
// Constructor
PartialsGraphDriver::PartialsGraphDriver() {}

void PartialsGraphDriver::init(xrt::device device, xrt::uuid & xclbin_uuid, int kernel_id)
{
    log_info("PartialsGraphDriver creating PL kernels...");
    
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

    // Set buffer size to 840 (+1 for packet ctrl data) VECTORS of floats
    // 840 is the Least Common Multiple (LCM) of netsizes 2 thru 8
    input_buffer  = xrt::bo(device, INPUT_PACKET_SIZE*sizeof(float), /*xrt::bo::flags::normal,*/ device_mm2s.group_id(0));
    result_buffer = xrt::bo(device, OUTPUT_PACKET_SIZE*sizeof(float), /*xrt::bo::flags::normal,*/ device_s2mm.group_id(0));

    // set kernel arguments
    run_device_mm2s.set_arg(0, input_buffer);
    run_device_mm2s.set_arg(1, INPUT_PACKET_SIZE); // extra size for ctrl data

    run_device_s2mm.set_arg(0, result_buffer);
    run_device_s2mm.set_arg(1, OUTPUT_PACKET_SIZE);
}

/*
 * Send packets of data to the PL data mover kernels.
 * Each packet consists of 8 floats, one coordinate from each net.
 * 
 * @param: input_start is an array of floats of size 8*net_size
 */
void PartialsGraphDriver::send_packet(float * packet)
{
    // DEBUG:
    //cout << endl << "Versal Driver send_packet to kernel:" << endl;;
    //cout << "Number of floats being sent: " << INPUT_PACKET_SIZE;
    //for(int j = 0; j < INPUT_PACKET_SIZE; j++) {
    //    if(j%8 == 0) cout << endl << j/8 << ": ";
    //    cout << packet[j] << " ";
    //}
    //cout << endl;

    start_time = getEpoch();
    input_buffer.write(packet);
    input_buffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    xfer_on_time = getTiming(getEpoch(), start_time);
    start_time = getEpoch();

    run_device_mm2s.start();
    run_device_s2mm.start();
}

float PartialsGraphDriver::receive_packet(float * output_data)
{
    run_device_mm2s.wait();
    run_device_s2mm.wait();
    kernel_exec_time = getTiming(getEpoch(), start_time);
    start_time = getEpoch();
    result_buffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    result_buffer.read(output_data);

    // DEBUG:
    //cout << endl;
    //cout << endl << "Versal Driver receiving packet to kernel:" << endl;;
    //cout << "Number of floats being received: " << OUTPUT_PACKET_SIZE;
    //for(int i = 0; i < OUTPUT_PACKET_SIZE; i++) {
    //    if(i%8 == 0) cout << endl;
    //    cout << output_data[i] << " ";
    //}
    //cout << endl;

    xfer_off_time=getTiming(getEpoch(), start_time);
}

void PartialsGraphDriver::print_info()
{
    printf("Kernel Total runtime : %f sec, (%f xfer on, %f execute, %f xfer off)\n",
        xfer_on_time + kernel_exec_time + xfer_off_time,
        xfer_on_time,
        kernel_exec_time,
        xfer_off_time);
}

long PartialsGraphDriver::getEpoch() {
  struct timeval tm;
  gettimeofday(&tm, NULL);
  return (tm.tv_sec * 1000000)+tm.tv_usec;
}

double PartialsGraphDriver::getTiming(long end_time, long start_time) {
  return (end_time - start_time) / 1.0e6;
}




// Constructor
DensityGraphDriver::DensityGraphDriver() {}

void DensityGraphDriver::init(xrt::device device, xrt::uuid & xclbin_uuid)
{
    log_info("DensityGraphDriver creating PL kernels...");
    
    // Create kernel objects
    // Be extra sure the names are correct, there might not be an error message!
    density_mm2s = xrt::kernel(device, xclbin_uuid, "density_mm2s");
    density_s2mm = xrt::kernel(device, xclbin_uuid, "density_s2mm");

    // Get memory bank groups for device buffers
    bank_input  = density_mm2s.group_id(0);
    bank_result = density_s2mm.group_id(0);


    // Create kernel runner instances
    run_device_mm2s = xrt::run(density_mm2s);
    run_device_s2mm = xrt::run(density_s2mm);

    // For density DCT computation, we transfer one row's worth of floats to compute DCT
    input_buffer  = xrt::bo(device, 2*BINS_PER_ROW*sizeof(float), /*xrt::bo::flags::normal,*/ density_mm2s.group_id(0));
    result_buffer = xrt::bo(device, 2*BINS_PER_ROW*sizeof(float), /*xrt::bo::flags::normal,*/ density_s2mm.group_id(0));

    // set kernel arguments
    run_device_mm2s.set_arg(0, input_buffer);
    run_device_mm2s.set_arg(1, 2*BINS_PER_ROW);

    run_device_s2mm.set_arg(0, result_buffer);
    run_device_s2mm.set_arg(1, 2*BINS_PER_ROW);
}

/*
 * Send packets of data to the PL data mover kernels.
 * Each packet consists of 8 floats, one coordinate from each net.
 * 
 * @param: input_start is an array of floats of size 8*net_size
 */
void DensityGraphDriver::send_packet(float * packet)
{
    // DEBUG:
    //cout << "Versal Driver sending packet to kernel:" << endl;
    //for(int j = 0; j < VEC_SIZE; j++)
    //    cout << packet[j] << " ";
    //cout << endl;

    start_time = getEpoch();
    input_buffer.write(packet);
    input_buffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    xfer_on_time = getTiming(getEpoch(), start_time);
    start_time = getEpoch();

    run_device_mm2s.start();
    run_device_mm2s.wait();
}

float DensityGraphDriver::receive_packet(float * output_data)
{
    run_device_s2mm.start();
    run_device_s2mm.wait();
    kernel_exec_time = getTiming(getEpoch(), start_time);

    start_time = getEpoch();
    result_buffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    //result_buffer.read(output_data); // TODO use output_data from VersalGraph
    result_buffer.read(output_data);

    // DEBUG:
    //cout << endl;
    //cout << "output_data: ";
    //for(int i = 0; i < VEC_SIZE; i++)
    //    cout << output_data[i] << " ";
    //cout << endl;

    xfer_off_time=getTiming(getEpoch(), start_time);
}

void DensityGraphDriver::print_info()
{
    printf("Kernel Total runtime : %f sec, (%f xfer on, %f execute, %f xfer off)\n",
        xfer_on_time + kernel_exec_time + xfer_off_time,
        xfer_on_time,
        kernel_exec_time,
        xfer_off_time);

    //// Debugging
    //printf("Results:\n");
    //for (int i=0; i < VEC_SIZE; i++) {
    //    printf("Output %d: %f\n", i, result_data[i]);
}

long DensityGraphDriver::getEpoch() {
  struct timeval tm;
  gettimeofday(&tm, NULL);
  return (tm.tv_sec * 1000000)+tm.tv_usec;
}

double DensityGraphDriver::getTiming(long end_time, long start_time) {
  return (end_time - start_time) / 1.0e6 ;
}

AIEPLACE_NAMESPACE_END