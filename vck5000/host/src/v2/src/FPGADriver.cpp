#include "FPGADriver.h"

namespace AIEPLACE_NAMESPACE {

  XRTDriver::XRTDriver(std::string xclbin_filename, std::string device_bdf) : xclbin_filename(xclbin_filename) {
    set_xclbin();
    set_device(device_bdf);
    load_xclbin();
  }

  void XRTDriver::set_xclbin() {
    this->xclbin = xrt::xclbin(this->xclbin_filename);
    this->target = this->xclbin.get_target_type();
  }
  void XRTDriver::load_xclbin() {
    this->xclbin_uuid = this->device.load_xclbin(this->xclbin);
  }

  void XRTDriver::set_device(std::string device_bdf) {
    switch(this->target) {
      case xrt::xclbin::target_type::sw_emu:
        this->device = xrt::device(0);
        break;
      case xrt::xclbin::target_type::hw_emu:
        this->device = xrt::device(0);
        break;
      case xrt::xclbin::target_type::hw:
        std::string bdf(device_bdf);
        this->device = xrt::device(bdf);
        break;
    }
    if(this->device == nullptr) {
      throw std::runtime_error("No valid device handle found. Run `xbutil examine` and look for the correct BDF.\n If xbutil is not found, then source the xrt setup file: `source /opt/xilinx/xrt/setup.sh`\n");
    }
  }

}
