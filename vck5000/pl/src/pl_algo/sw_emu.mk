# sw_emu.mk -- TEMPORARY PL-only sw_emu packaging for the pl_algo v0 HPWL bring-up.
#
# v0 is pure PL (no AIE graph yet), but the shared vck5000 Makefile assumes an AIE
# libadf as a link input for the xsa/xclbin steps. Rather than bend the shared
# flow around this temporary case, this standalone makefile does the minimal
# PL-only 3-step Versal flow (v++ -c -> -l -> -p) + emconfig, mirroring the proven
# ~/phd/toy_design template.
#
# DELETE this once aie/src/pl_algo exists and v0 folds into the normal
# xsa/xclbin flow in the top-level Makefile.
#
# Prereqs (source first):
#   source /tools/Xilinx/Vitis/2022.2/settings64.sh
#   source /opt/xilinx/xrt/setup.sh
#   export PLATFORM_REPO_PATHS=$HOME/xilinx_local/opt/xilinx/platforms
#
# Usage (from vck5000/pl/src/pl_algo):
#   make -f sw_emu.mk            # build xclbin + emconfig into build_v0/
#   make -f sw_emu.mk clean

PLATFORM ?= xilinx_vck5000_gen4x8_qdma_2_202220_1
TARGET   := sw_emu
KERNEL   := top
OUT      := build_v0

XO     := $(OUT)/$(KERNEL).$(TARGET).xo
XSA    := $(OUT)/$(KERNEL).$(TARGET).xsa
XCLBIN := $(OUT)/$(KERNEL).$(TARGET).xclbin

# host_interface.hpp lives in src/. For sw_emu the linker RE-compiles the kernel
# from source, so the include path must be given to the -l step too (absolute,
# since v++ recompiles in its own temp dir).
KINC := -I$(CURDIR)/src

.PHONY: all
all: $(XCLBIN) $(OUT)/emconfig.json
	@echo "v0 xclbin ready: $(XCLBIN)"

$(OUT):
	mkdir -p $(OUT)

# 1. HLS kernel -> .xo
$(XO): src/$(KERNEL).cpp | $(OUT)
	v++ -c -t $(TARGET) --platform $(PLATFORM) $(KINC) -k $(KERNEL) $< -o $@

# 2. link -> .xsa  (PL-only: a single .xo, no AIE libadf, no PL<->AIE connectivity)
$(XSA): $(XO)
	v++ -l -t $(TARGET) --platform $(PLATFORM) $(KINC) $< -o $@

# 3. package -> .xclbin
$(XCLBIN): $(XSA)
	v++ -p -t $(TARGET) --platform $(PLATFORM) $< -o $@

# 4. emulation config (describes the emulated device to XRT)
$(OUT)/emconfig.json: | $(OUT)
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(OUT)

# 5. run the host under sw_emu. The host exe is built separately:
#      make host HOST=pl_algo BUILD_XRT=1
# sw_emu is selected by XCL_EMULATION_MODE (not the xclbin); EMCONFIG_PATH points
# XRT at the emulated-device description; LD_LIBRARY_PATH adds the sw_emu shims.
# Override BENCH=<dir> for a different design (keep it small -- sw_emu sims the
# kernel in software).
HOST_EXE ?= $(CURDIR)/../../../build/hw/host/pl_algo/aieplace_pl_algo.exe
BENCH    ?= $(CURDIR)/../../../host/benchmarks/ispd2015/mgc_pci_bridge32_b

.PHONY: run
run: all
	XCL_EMULATION_MODE=$(TARGET) \
	EMCONFIG_PATH=$(CURDIR)/$(OUT) \
	LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
	$(HOST_EXE) $(BENCH) $(XCLBIN)

.PHONY: clean
clean:
	rm -rf $(OUT) _x .Xil *.log *.jou *.link_summary *.compile_summary package.* *.pb
