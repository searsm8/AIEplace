#ifndef VCK5000DRIVER_H
#define VCK5000DRIVER_H

#include "Common.h"
#include "Net.h"

#include "xrt/xrt_uuid.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"
#include "xrt/xrt_aie.h" // for Graph APIs
#include <stdexcept>

// On AIEs, we process only nets of size 2 thru 8. This covers the great majority of all nets
// other nets above size 8 will be processed on the host.
#define MIN_AIE_NET_SIZE 2
#define MAX_AIE_NET_SIZE 8


AIEPLACE_NAMESPACE_BEGIN

struct DeviceNetlist
{
  uint8_t min_net_size = MIN_AIE_NET_SIZE;
  uint8_t max_net_size = MAX_AIE_NET_SIZE;
  uint32_t* num_nets_per_net_size;
  uint32_t* nets;
};

struct DeviceNodeCoords
{
  uint32_t num_nodes;
  float* node_x;
  float* node_y;
};

class VCK5000Driver
{
public:
    VCK5000Driver(char* xclbin_filename, char* device_bdf);
    ~VCK5000Driver();

    void set_nodes();
    void set_netlist(std::map<int, std::vector<Net*>>& nets_by_degree);

    void send_netlist();
    void send_nodes();
    void receive_partial_derivative_HPWL(float* data);

    void print_info();

private:
    std::string xclbin_file;
    xrt::xclbin xclbin;
    xrt::uuid xclbin_uuid;
    xrt::device device;
    xrt::xclbin::target_type target;

    void set_xclbin(std::string& xclbin_file);
    void load_xclbin();
    void set_device(char* device_bdf);

    template <typename T>
    void sync_h2d(xrt::bo& bo, T* data);
    template <typename T>
    void sync_d2h(xrt::bo& bo, T* data);

    // long start_time, xfer_on_time, xfer_off_time, kernel_exec_time;
    xrt::kernel device_mm2s;
    xrt::kernel device_s2mm;

    // buffer objects to hold inputs and outputs
    xrt::bo nodes_buffer;
    xrt::bo netlist_buffer;
    xrt::bo result_buffer;

    xrt::run run_device_mm2s;
    xrt::run run_device_s2mm;

    xrtMemoryGroup bank_input;
    xrtMemoryGroup bank_result;

    DeviceNetlist netlist;
    DeviceNodeCoords nodes;

    std::map<std::string, uint32_t> node_indeces;
};

AIEPLACE_NAMESPACE_END

#endif

