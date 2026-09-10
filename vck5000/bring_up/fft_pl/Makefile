# fft_pl -- PL-only hardware harness for pl_algo's FFT-based 1D transform module. NO AIE.
#
# Purpose: add1_pl proved the PL load/run path on the hw node's VCK5000 (2022.2-build,
# 2024.2-run). This is the next rung: take a REAL pl_algo module --
# vck5000/pl/src/pl_algo/src/modules/fft_pl.hpp (plalgo::dct_1d_pl/idct_1d_pl/idxst_1d_pl, a
# float port of the Makhoul DCT/IDCT/IDXST built on an in-house radix-2 FFT) -- and run it on
# real hardware, still with zero AIE graph, zero stream ports, zero link.cfg connectivity. The
# module is included directly (-I below), not copied, so it stays in sync with pl_algo.
#
# Verified offline first (tier 1, per CLAUDE.md's Verification Loop):
#   cd ../vck5000 && make test   # runs vck5000/test/fft_pl_test.cpp, g++ only, seconds
# This harness is the SAME check (naive double golden, 1e-6 rel_rms tolerance), run through
# XRT on real silicon instead of a native call.
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

# Transform length N, baked into fft_pl.hpp's FFT_N/FFT_LOG at compile time (both the PL kernel
# and the host must agree -- passed to both builds below). 64 matches vck5000/test/fft_pl_test.cpp's
# small-grid tier-1 check (fast synth/link; the 1024 throughput datapath stays AIE-assisted).
PL_GRID ?= 64

# Real module lives in the pl_algo tree; include it, don't copy it.
PL_ALGO_SRC = ../vck5000/pl/src/pl_algo/src

VPP_TEMPS = --temp_dir $(BUILD)/_x --log_dir $(BUILD)/logs --report_dir $(BUILD)/reports

TOP_XO  = $(BUILD)/top.$(TARGET).xo
XSA     = $(BUILD)/fft_pl.$(TARGET).xsa
XCLBIN  = $(BUILD)/fft_pl.$(TARGET).xclbin
HOST    = $(BUILD)/host

.PHONY: all pl host xclbin csynth run clean
all: $(XCLBIN) $(HOST) $(BUILD)/emconfig.json

pl:     $(TOP_XO)
host:   $(HOST)
xclbin: $(XCLBIN)

# ---- tier 1: PL C-synthesis gate (fast; no link/package) --------------------
csynth: $(TOP_XO)

# 1. PL kernel -> .xo
$(TOP_XO): src/pl/top.cpp $(PL_ALGO_SRC)/modules/fft_pl.hpp
	@mkdir -p $(BUILD)
	v++ -c -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) -I$(PL_ALGO_SRC) -DPL_GRID=$(PL_GRID) \
		-k fft_pl_top src/pl/top.cpp -o $@

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
	g++ -std=c++17 -Wall -O2 -DPL_GRID=$(PL_GRID) -o $@ src/host/host.cpp \
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
		./host fft_pl.$(TARGET).xclbin

clean:
	rm -rf build .Xil *.log *.jou
