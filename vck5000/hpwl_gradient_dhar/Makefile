# Standalone Vitis build for the Dhar HPWL-gradient kernel (pure PL, no AIE).
#
# Prereqs (source before running):
#   source /tools/Xilinx/Vitis/2022.2/settings64.sh
#   source /opt/xilinx/xrt/setup.sh
#
# Usage:
#   make all               # build kernel + xclbin + host + emconfig  (TARGET=sw_emu default)
#   make run               # run host under sw_emu and verify vs the CPU golden
#   make TARGET=hw all     # build for real hardware (long; see run_hw.sh)
#   make clean
#
# The kernel #includes the pl_algo module in place (INC), so a module edit rebuilds the kernel --
# there is no copy of the source here, same contract as the tier-1 harness.

PLATFORM   ?= xilinx_vck5000_gen4x8_qdma_2_202220_1
TARGET     ?= sw_emu
XILINX_XRT ?= /opt/xilinx/xrt

KERNEL = hpwl_gradient_dhar_top
INC    = -I../pl/src/pl_algo/src          # module root: "modules/..." and "host_interface.hpp"

XO     = $(KERNEL).$(TARGET).xo
XSA    = $(KERNEL).$(TARGET).xsa
XCLBIN = $(KERNEL).$(TARGET).xclbin
HOST   = host

.PHONY: all
all: $(XCLBIN) $(HOST) emconfig.json

# 1. Compile the HLS kernel to a .xo
$(XO): src/$(KERNEL).cpp
	v++ -c -t $(TARGET) --platform $(PLATFORM) -k $(KERNEL) $(INC) $< -o $@

# 2. Link the .xo into a .xsa
$(XSA): $(XO)
	v++ -l -t $(TARGET) --platform $(PLATFORM) $< -o $@

# 3. Package the .xsa into an xclbin
$(XCLBIN): $(XSA)
	v++ -p --platform $(PLATFORM) -t $(TARGET) $< -o $@

# 4. Build the host (native XRT C++ API). Needs the module root for the shared POD types + refresh.
$(HOST): src/host.cpp
	g++ -std=c++17 -Wall -O2 -o $@ $< $(INC) \
		-I$(XILINX_XRT)/include -L$(XILINX_XRT)/lib \
		-lxrt_coreutil -pthread

# 5. Emulation config
emconfig.json:
	emconfigutil --platform $(PLATFORM) --nd 1

.PHONY: run
run: all
	XCL_EMULATION_MODE=$(TARGET) \
	LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
	./$(HOST) $(XCLBIN)

.PHONY: clean
clean:
	rm -rf $(HOST) *.xo *.xsa *.xclbin *.ltx *.log *.jou _x .Xil emconfig.json \
		*.info *.link_summary *.compile_summary package.* *.pb *.run_summary
