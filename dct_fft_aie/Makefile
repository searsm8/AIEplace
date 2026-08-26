# dct_fft_aie -- DCT-transpose (PL) <-> forward-FFT (AIE) integration test for the VCK5000.
# Self-contained: everything it builds is under ./$(BUILD); no dependency on the AIEplace tree.
#
# Prereqs (source before running):
#   source /tools/Xilinx/Vitis/2022.2/settings64.sh
#   source /opt/xilinx/xrt/setup.sh
#
# Usage:
#   make golden               # pure-g++ check of the double reference (no Vitis), seconds
#   make csynth               # v++ -c of the PL top only (fast correctness-of-synthesis gate)
#   make TARGET=sw_emu run     # full build + x86-AIE-model emulation on this box  (default)
#   make TARGET=hw all         # build the real-silicon xclbin for Geert's card
#   make clean

PLATFORM_NAME ?= xilinx_vck5000_gen4x8_qdma_2_202220_1
PLATFORM_REPO ?= $(HOME)/xilinx_local/opt/xilinx/platforms
PLATFORM      ?= $(PLATFORM_REPO)/$(PLATFORM_NAME)/$(PLATFORM_NAME).xpfm
TARGET        ?= sw_emu
XILINX_XRT    ?= /opt/xilinx/xrt
XILINX_VITIS  ?= /tools/Xilinx/Vitis/2022.2
DSPLIB_ROOT   ?= /tools/Xilinx/Model_Composer/2022.2/tps/xf_dsp
BUILD         ?= build/$(TARGET)

# 8-lane FFT pool (must match FFT_LANES in src/pl/formats.hpp).
FFT_LANES ?= 8

# AIE compiles to a hardware archive for hw/hw_emu, x86 model for sw_emu.
ifeq ($(TARGET),sw_emu)
  AIE_TARGET := x86sim
else
  AIE_TARGET := hw
endif

VPP_TEMPS = --temp_dir $(BUILD)/_x --log_dir $(BUILD)/logs --report_dir $(BUILD)/reports

# Kernel-header search path. Needed at BOTH -c and -l: in sw_emu the linker re-compiles the
# kernel .cpp from a flattened cpu_sources/ dir and must still find formats.hpp / modules/.
# Absolute so it resolves regardless of v++'s working directory.
KERNEL_INC = -I$(CURDIR)/src/pl

AIE_FLAGS  = --include="src/aie"
AIE_FLAGS += --include="$(XILINX_VITIS)/aietools/include"
AIE_FLAGS += --include="$(DSPLIB_ROOT)/L1/include/aie"
AIE_FLAGS += --include="$(DSPLIB_ROOT)/L1/src/aie"
AIE_FLAGS += --include="$(DSPLIB_ROOT)/L2/include/aie"
AIE_FLAGS += --Xpreproc="-DDENSITY_INSTANCES=$(FFT_LANES)"

LIBADF  = $(BUILD)/libadf.a
TOP_XO  = $(BUILD)/top.$(TARGET).xo
XSA     = $(BUILD)/dct_fft_aie.$(TARGET).xsa
XCLBIN  = $(BUILD)/dct_fft_aie.$(TARGET).xclbin
HOST    = $(BUILD)/host

.PHONY: all aie pl host xclbin golden csynth run clean
all: $(XCLBIN) $(HOST) $(BUILD)/emconfig.json

aie:    $(LIBADF)
pl:     $(TOP_XO)
host:   $(HOST)
xclbin: $(XCLBIN)

# ---- tier 1: pure-g++ golden self-check (no Vitis) --------------------------
golden:
	@mkdir -p $(BUILD)
	g++ -std=c++17 -O2 -Isrc/host test/golden_test.cpp -o $(BUILD)/golden_test
	$(BUILD)/golden_test

# ---- tier 2: PL C-synthesis gate (fast; no link/package) --------------------
csynth: $(TOP_XO)

# 1. AIE graph -> libadf.a
$(LIBADF): src/aie/graph.cpp src/aie/DensityFFTGraph.h src/aie/system_settings.h
	@mkdir -p $(BUILD)
	aiecompiler --target=$(AIE_TARGET) --platform=$(PLATFORM) $(AIE_FLAGS) \
		--workdir=$(BUILD)/Work $< --output-archive $(abspath $@)

# 2. PL top kernel -> .xo  (includes resolve relative to src/pl/)
$(TOP_XO): src/pl/top.cpp src/pl/formats.hpp src/pl/host_interface.hpp \
           src/pl/modules/dct_transpose.hpp
	@mkdir -p $(BUILD)
	v++ -c -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) $(KERNEL_INC) -k top src/pl/top.cpp -o $@

# 3. Link PL kernel + AIE archive -> .xsa
$(XSA): $(TOP_XO) $(LIBADF) link.cfg
	v++ -l -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) $(KERNEL_INC) \
		--config link.cfg $(TOP_XO) $(LIBADF) -o $@

# 4. Package -> .xclbin (includes the AIE libadf.a)
$(XCLBIN): $(XSA) $(LIBADF)
	v++ -p -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) \
		--package.out_dir $(BUILD)/package $(XSA) $(LIBADF) -o $@

# 5. Host (native XRT + graph API)
$(HOST): src/host/host.cpp src/host/golden.hpp
	@mkdir -p $(BUILD)
	g++ -std=c++17 -Wall -O2 -o $@ src/host/host.cpp -Isrc/host \
		-I$(XILINX_XRT)/include -L$(XILINX_XRT)/lib -lxrt_coreutil -pthread

# 6. Emulation config
$(BUILD)/emconfig.json:
	@mkdir -p $(BUILD)
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD)

# Emulation run (sw_emu only on this platform: hw_emu lacks xclGraphOpen on VCK5000 QDMA).
run: all
	cd $(BUILD) && XCL_EMULATION_MODE=$(TARGET) \
		LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
		./host dct_fft_aie.$(TARGET).xclbin dct

clean:
	rm -rf build .Xil *.log *.jou Work
