# add1_pl -- pure-PL "hello world" for the VCK5000. NO AIE anywhere.
#
# Purpose: dct_fft_aie's HANDOFF.md documents a 2022.2-built xclbin (with
# AIE_METADATA/AIE_RESOURCES/BITSTREAM_PARTIAL_PDI) failing to load on the hw
# node's 2024.2 XRT (`err = -22`), with the working theory being that it's
# specifically the AIE CDO/PDI array-config download the 2024.2 driver stack
# rejects -- not the base bitstream (XRT's own AIE-less verify.xclbin, built
# the same way, loads fine there). This design tests that theory directly: one
# PL kernel, two DDR ports, a scalar arg, zero AIE graph, zero stream ports,
# zero link.cfg connectivity.
#
#   - If this loads and runs on the hw node -> the AIE-CDO theory holds, and
#     the fix has to happen on the AIE side (older XRT, format shim, etc).
#   - If this ALSO fails with err -22 -> the AIE theory is wrong; the 2022.2
#     build / 2024.2 run combination doesn't work at all for this platform,
#     full stop, regardless of AIE.
#
# Prereqs (source before running):
#   source /tools/Xilinx/Vitis/2022.2/settings64.sh
#   source /opt/xilinx/xrt/setup.sh
#
# Usage:
#   make csynth               # v++ -c of the PL kernel only (fast synthesis gate)
#   make TARGET=sw_emu run     # full build + sw_emu functional check  (default)
#   make TARGET=hw all         # build the real-silicon xclbin
#   make clean

PLATFORM_NAME ?= xilinx_vck5000_gen4x8_qdma_2_202220_1
PLATFORM_REPO ?= $(HOME)/xilinx_local/opt/xilinx/platforms
PLATFORM      ?= $(PLATFORM_REPO)/$(PLATFORM_NAME)/$(PLATFORM_NAME).xpfm
TARGET        ?= sw_emu
XILINX_XRT    ?= /opt/xilinx/xrt
XILINX_VITIS  ?= /tools/Xilinx/Vitis/2022.2
BUILD         ?= build/$(TARGET)

VPP_TEMPS = --temp_dir $(BUILD)/_x --log_dir $(BUILD)/logs --report_dir $(BUILD)/reports

TOP_XO  = $(BUILD)/top.$(TARGET).xo
XSA     = $(BUILD)/add1_pl.$(TARGET).xsa
XCLBIN  = $(BUILD)/add1_pl.$(TARGET).xclbin
HOST    = $(BUILD)/host

.PHONY: all pl host xclbin csynth run clean
all: $(XCLBIN) $(HOST) $(BUILD)/emconfig.json

pl:     $(TOP_XO)
host:   $(HOST)
xclbin: $(XCLBIN)

# ---- tier 1: PL C-synthesis gate (fast; no link/package) --------------------
csynth: $(TOP_XO)

# 1. PL kernel -> .xo
$(TOP_XO): src/pl/top.cpp
	@mkdir -p $(BUILD)
	v++ -c -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) -k add1 src/pl/top.cpp -o $@

# 2. Link -> .xsa  (single kernel, no streams/connectivity -- no --config needed)
$(XSA): $(TOP_XO)
	v++ -l -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) $(TOP_XO) -o $@

# 3. Package -> .xclbin
$(XCLBIN): $(XSA)
	v++ -p -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) \
		--package.out_dir $(BUILD)/package $(XSA) -o $@

# 4. Host (native XRT)
$(HOST): src/host/host.cpp
	@mkdir -p $(BUILD)
	g++ -std=c++17 -Wall -O2 -o $@ src/host/host.cpp \
		-I$(XILINX_XRT)/include -L$(XILINX_XRT)/lib -lxrt_coreutil -luuid -pthread

# 5. Emulation config
$(BUILD)/emconfig.json:
	@mkdir -p $(BUILD)
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD)

# Emulation run. Unlike dct_fft_aie, this has no AIE graph, so hw_emu should
# also work here (just pass TARGET=hw_emu) -- sw_emu stays the default gate.
run: all
	cd $(BUILD) && XCL_EMULATION_MODE=$(TARGET) \
		LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
		./host add1_pl.$(TARGET).xclbin

clean:
	rm -rf build .Xil *.log *.jou
