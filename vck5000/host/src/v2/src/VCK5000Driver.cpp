#include "VCK5000Driver.h"

AIEPLACE_NAMESPACE_BEGIN

VCK5000Driver::VCK5000Driver(char* xclbin_filename, char* device_bdf) {
  this->xclbin_file = xclbin_filename;
  set_xclbin(this->xclbin_file);
  set_device(device_bdf);
  load_xclbin();
}

void VCK5000Driver::set_xclbin(std::string& xclbin_file) {
  this->xclbin = xrt::xclbin(xclbin_file);
  this->target = this->xclbin.get_target_type();
}
void VCK5000Driver::load_xclbin() {
  this->xclbin_uuid = this->device.load_xclbin(this->xclbin);
}

void VCK5000Driver::set_device(char* device_bdf) {
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

void VCK5000Driver::set_netlist(std::map<int, std::vector<Net*>>& nets_by_degree)
{
  for (uint i = netlist.min_net_size; i < netlist.max_net_size; i++)
  {
      nets_by_degree[i].size();
  }
}

void VCK5000Driver::set_nodes(


AIEPLACE_NAMESPACE_END
