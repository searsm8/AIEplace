# iteration_pl -- PL-only hardware harness for ONE full pl_algo Nesterov placement step
# (HPWL gradient + density gradient, combined and stepped). NO AIE.
#
# Purpose: the next rung above ../hpwl_pl (HPWL gradient alone) and ../field_solve_pl
# (density field solve alone) -- chains hpwl_CU, density_bin, field_solve_pl, force_gather
# and iteration_update/memory_writer into one kernel, one call = one placement step. Zero
# AIE graph, zero stream ports, zero link.cfg connectivity. Every module is included
# directly (-I below), not copied, so it stays in sync with pl_algo.
#
# This is NOT the device-resident Stage 5 loop (vck5000/pl/src/pl_algo/DATAFLOW.md) -- no
# on-chip schedule/convergence, no looping. lambda/alpha/coeff are host-supplied scalars for
# a single step, matching pl_algo's v1 host-owned policy. See DATAFLOW.md and task #20
# (.claude/0_WORKFLOW/tasks.md) for why composing that resident loop is explicitly deferred.
#
# There is no vck5000/test/*iteration* tier-1 harness -- this design's host.cpp is its own
# golden, built from the same math each already-verified sw_emu check
# (HpwlGradVerify/ForceVerify/IterVerify in vck5000/host/src/pl_algo/src/) already uses.
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

# Small-grid build: PL_GRID sizes density_bin/field_solve_pl's on-chip matrices (default 64,
# matching ../field_solve_pl); ITER_MAX_NODES bounds the on-chip node_pos/gradient scratch
# hpwl_CU and force_gather need (default 128; must be >= num_nodes the host uploads).
PL_GRID        ?= 64
ITER_MAX_NODES ?= 128

# Real modules live in the pl_algo tree; include them, don't copy them.
PL_ALGO_SRC = ../vck5000/pl/src/pl_algo/src

VPP_TEMPS = --temp_dir $(BUILD)/_x --log_dir $(BUILD)/logs --report_dir $(BUILD)/reports

TOP_XO  = $(BUILD)/top.$(TARGET).xo
XSA     = $(BUILD)/iteration_pl.$(TARGET).xsa
XCLBIN  = $(BUILD)/iteration_pl.$(TARGET).xclbin
HOST    = $(BUILD)/host

.PHONY: all pl host xclbin csynth run clean
all: $(XCLBIN) $(HOST) $(BUILD)/emconfig.json

pl:     $(TOP_XO)
host:   $(HOST)
xclbin: $(XCLBIN)

# ---- tier 1: PL C-synthesis gate (fast; no link/package) --------------------
csynth: $(TOP_XO)

# 1. PL kernel -> .xo
$(TOP_XO): src/pl/top.cpp $(PL_ALGO_SRC)/modules/hpwl_gradient.hpp \
           $(PL_ALGO_SRC)/modules/density_bin.hpp $(PL_ALGO_SRC)/modules/field_solve_pl.hpp \
           $(PL_ALGO_SRC)/modules/force_gather.hpp $(PL_ALGO_SRC)/modules/iteration_update.hpp \
           $(PL_ALGO_SRC)/modules/memory_writer.hpp $(PL_ALGO_SRC)/host_interface.hpp
	@mkdir -p $(BUILD)
	v++ -c -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) -I$(PL_ALGO_SRC) \
		-DPL_GRID=$(PL_GRID) -DITER_MAX_NODES=$(ITER_MAX_NODES) \
		-k iteration_top src/pl/top.cpp -o $@

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
	g++ -std=c++17 -Wall -O2 -I$(PL_ALGO_SRC) -DPL_GRID=$(PL_GRID) -o $@ src/host/host.cpp \
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
		./host iteration_pl.$(TARGET).xclbin

clean:
	rm -rf build .Xil *.log *.jou
