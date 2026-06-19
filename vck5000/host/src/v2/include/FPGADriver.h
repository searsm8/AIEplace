#ifndef FPGADRIVER_H
#define FPGADRIVER_H

#include "Common.h"
#include "Library.h"
#include "DataBase.h"

#include "xrt/xrt_uuid.h"
#include "xrt/xrt_device.h"
#include "xrt/experimental/xrt_xclbin.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"
#include "xrt/xrt_aie.h" // for Graph APIs
#include <stdexcept>

// On AIEs, we process only nets of size 2 thru 8. This covers the great majority of all nets
// other nets above size 8 will be processed on the host.
#define MIN_AIE_NET_SIZE 2
#define MAX_AIE_NET_SIZE 8


namespace AIEPLACE_NAMESPACE {

  struct DeviceComponent {
    float x;
    float y;
    uint8_t type_id;
  };

  struct DeviceComponentType {
    float width;
    float height;
    uint16_t pin_base;   // index into global PinOffset[] array
    uint16_t pin_count;  // number of pins for this type
  };

  struct PinOffset {
    float dx;
    float dy;
  };

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

  struct DeviceNodeTypes
  {
  };

  class IFPGADriver {
    public:
      virtual ~IFPGADriver() = default;

      // Called once, e.g. after construction
      //virtual void initialize() = 0;

      //// Send data to the FPGA
      //virtual void send_types(const void* data, std::size_t bytes) = 0;
      //virtual void send_components(const void* data, std::size_t bytes) = 0;
      //virtual void send_netlist(const void* data, std::size_t bytes) = 0;

      //// Run the kernel(s)
      //virtual void run_HPWL_kernel(std::size_t num_results) = 0;

      //// Receive results from FPGA to host
      //virtual void receive_HPWL_results(void* out_data, std::size_t bytes) = 0;
  };

  class MockFPGADriver : public IFPGADriver
  {
    public:
      MockFPGADriver() {}
      ~MockFPGADriver() override = default;
  };

  class XRTDriver : public IFPGADriver
  {
    public:
      XRTDriver(std::string xclbin_filename, std::string device_bdf);
      ~XRTDriver() override = default;

    private:
      std::string xclbin_filename;
      xrt::xclbin xclbin;
      xrt::uuid xclbin_uuid;
      xrt::device device;
      xrt::xclbin::target_type target;

      void set_xclbin();
      void load_xclbin();
      void set_device(std::string device_bdf);

      template <typename T>
        void sync_h2d(xrt::bo& bo, T* data);
      template <typename T>
        void sync_d2h(xrt::bo& bo, T* data);

      // long start_time, xfer_on_time, xfer_off_time, kernel_exec_time;
      xrt::kernel device_mm2s;
      xrt::kernel device_s2mm;

      // buffer objects to hold inputs and outputs
      xrt::bo types_buffer;
      xrt::bo components_buffer;
      xrt::bo netlist_buffer;
      xrt::bo result_buffer;

      xrt::run run_device_mm2s;
      xrt::run run_device_s2mm;

      xrtMemoryGroup bank_input;
      xrtMemoryGroup bank_result;

  };

}

#endif
