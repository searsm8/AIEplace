#######################################################################################
.PHONY: help
help:
	@echo "Makefile Usage:"
	@echo "  make build TARGET=<sw_emu/hw_emu/hw>"
	@echo "      Command to generate the design for specified Target and Device."
	@echo ""
	@echo "  make host "
	@echo "      Command to generate host."
	@echo ""
	@echo "  make device "
	@echo "      Command to generate hardware platform files(xo,xclbin)."
	@echo ""
	@echo "  make clean "
	@echo "      Command to remove the generated files."
	@echo ""
#######################################################################################
HOST_EXE := host

# Host building global settings
CXXFLAGS := -I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include/ -I/lustre/home/nx08/shared/fpga/xilinx/include/ -Wall -O0 -g -std=c++17 -L$(XILINX_XRT)/lib/ -lpthread -lrt -lstdc++ 
CXXFLAGS2 := -lxrt_coreutil -lxilinxopencl

.PHONY: $(HOST_EXE)
# Building Host
$(HOST_EXE): src/host.cpp
	mkdir -p ../bin
	g++ $(CXXFLAGS) -o ../bin/host '$<' $(CXXFLAGS2)

.PHONY: emconfig
emconfig:
	emconfigutil --platform $(PLATFORM)

# Cleaning stuff
.PHONY: clean
clean:
	rm -f $(HOST_EXE) *.log *.json *.xo
