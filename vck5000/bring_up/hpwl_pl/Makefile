# hpwl_pl -- PL-only hardware harness for pl_algo's HPWL gradient compute unit. NO AIE.
#
# Purpose: put vck5000/pl/src/pl_algo/src/modules/hpwl_gradient.hpp (plalgo::hpwl_CU: the
# three-segmented-reduction WA-HPWL gradient) on real silicon standalone, same rung as
# ../fft_pl and ../field_solve_pl. Zero AIE graph, zero stream ports, zero link.cfg
# connectivity. The module is included directly (-I below), not copied, so it stays in sync
# with pl_algo.
#
# There is no vck5000/test/*hpwl* tier-1 harness yet (unlike fft_pl_test.cpp /
# field_solve_test.cpp) -- this design's host.cpp is its own golden (exact-exp full-WA
# gradient, same math as vck5000/host/src/pl_algo/src/HpwlGradVerify.cpp's sw_emu check).
#
# Prereqs (source before running):
#   source /tools/Xilinx/Vitis/2022.2/settings64.sh
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

# Real module lives in the pl_algo tree; include it, don't copy it.
PL_ALGO_SRC = ../vck5000/pl/src/pl_algo/src

VPP_TEMPS = --temp_dir $(BUILD)/_x --log_dir $(BUILD)/logs --report_dir $(BUILD)/reports

TOP_XO  = $(BUILD)/top.$(TARGET).xo
XSA     = $(BUILD)/hpwl_pl.$(TARGET).xsa
XCLBIN  = $(BUILD)/hpwl_pl.$(TARGET).xclbin
HOST    = $(BUILD)/host

.PHONY: all pl host xclbin csynth run clean
all: $(XCLBIN) $(HOST) $(BUILD)/emconfig.json

pl:     $(TOP_XO)
host:   $(HOST)
xclbin: $(XCLBIN)

# ---- tier 1: PL C-synthesis gate (fast; no link/package) --------------------
csynth: $(TOP_XO)

# 1. PL kernel -> .xo
$(TOP_XO): src/pl/top.cpp $(PL_ALGO_SRC)/modules/hpwl_gradient.hpp $(PL_ALGO_SRC)/host_interface.hpp
	@mkdir -p $(BUILD)
	v++ -c -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) -I$(PL_ALGO_SRC) \
		-k hpwl_top src/pl/top.cpp -o $@

# 2. Link -> .xsa  (single kernel, no streams/connectivity -- no --config needed)
# PROFILE=1 adds AXI Performance Monitors (see the `hw_profile` target below); default
# builds pass no extra flags, so this is a no-op for the normal TARGET=hw / sw_emu path.
PROFILE_KERNEL ?= data:hpwl_top:hpwl_top_1:all
ifeq ($(PROFILE),1)
VPP_LINK_FLAGS += --profile_kernel $(PROFILE_KERNEL)
endif

$(XSA): $(TOP_XO)
	v++ -l -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) $(VPP_LINK_FLAGS) $(TOP_XO) -o $@

# 3. Package -> .xclbin
$(XCLBIN): $(XSA)
	v++ -p -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) \
		--package.out_dir $(BUILD)/package $(XSA) -o $@

# 4. Host (native XRT)
$(HOST): src/host/host.cpp
	@mkdir -p $(BUILD)
	g++ -std=c++17 -Wall -O2 -I$(PL_ALGO_SRC) -o $@ src/host/host.cpp \
		-I$(XILINX_XRT)/include -L$(XILINX_XRT)/lib -lxrt_coreutil -luuid -pthread

# 5. Emulation config
$(BUILD)/emconfig.json:
	@mkdir -p $(BUILD)
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD)

# Emulation run. No AIE graph, so hw_emu should also work here (pass TARGET=hw_emu); sw_emu
# stays the default gate.
run: all
	cd $(BUILD) && XCL_EMULATION_MODE=$(TARGET) \
		LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
		./host hpwl_pl.$(TARGET).xclbin

clean:
	rm -rf build .Xil *.log *.jou

# ---- Profiling build (Path B, build/hw/HANDOFF.md) -------------------------------------
# Real hw bitstream, but with AXI Performance Monitors instrumented at link so
# device_trace_*.csv / summary.csv actually populate per-port throughput. Kept in its OWN
# build dir and its OWN target so it never touches build/hw, which may be mid-use by
# another agent (that build/hw has no monitors and its .ltx can't address this xclbin's,
# or vice versa -- the two builds must not mix outputs). Needs the build node (v++/vitis_hls),
# same as any TARGET=hw build.
PROFILE_BUILD ?= build/hw_profile

.PHONY: hw_profile
hw_profile:
	$(MAKE) TARGET=hw BUILD=$(PROFILE_BUILD) PROFILE=1 all
	@printf '[Debug]\ndevice_trace=fine\n' > $(PROFILE_BUILD)/xrt.ini
