# Top level Makefile which calls subdirectory Makefiles

PROJECT_ROOT := $(CURDIR)

# Custom config files, to avoid having to put too many varialbes on the cli
ifdef INPUT_FILE
include configs/$(INPUT_FILE)
endif

include $(PROJECT_ROOT)/common.mk
include $(PL_DIR)/makeflags.mk

.PHONY: help
help:
	@echo ""
	@echo "*#*# Makefile for vck5000 implementation of AIEplace *#*#"
	@echo ""
	@echo "Current configuration of variables:"
	@echo -e '\tTARGET=$(TARGET)\t\t\t The build target: sw_emu, hw_emu or hw'
	@echo -e '\tAIE=$(AIE)\t\t\t The AIE code version selected from aie/src/'
	@echo -e '\tPL=$(PL)\t\t\t The PL code version selected from pl/src/'
	@echo -e '\tHOST=$(HOST)\t\t\t The HOST code version selected from host/src/'
	@echo -e '\tAIE_PARTIALS_INSTANCES=$(AIE_PARTIALS_INSTANCES)\t The number of Partials accelerator instances in the AIE Graph.'
	@echo -e '\tAIE_DENSITY_INSTANCES=$(AIE_DENSITY_INSTANCES)\t The number of Density accelerator instances in the AIE Graph.'
	@echo ""
	@echo 'You can build the different parts of the application with these commands:'
	@echo -e '\tmake aie:\t Build the AIE graph configuration.'
	@echo -e '\tmake pl:\t Build HLS kernels for the PL.'
	@echo -e '\tmake xsa:\t Link PL kernels and AIE Graph into XSA file.'
	@echo -e '\tmake xclbin:\t Package XSA for use on VCK5000.'
	@echo -e '\tmake host:\t Builds the host code which runs on a host x86 machine.'
	@echo -e '\tmake all:\t Builds the the entire application (xclbin and host).'
	@echo ""

.PHONY: all xclbin xsa aie pl host
all: xclbin host
xclbin: $(XCLBIN)
xsa: $(XSA)

host:
	$(MAKE) -C host PROJECT_ROOT=$(PROJECT_ROOT) HOST=$(HOST)

aie:
	$(MAKE) -C aie all \
		TARGET=$(TARGET) AIE=$(AIE) \
		PROJECT_ROOT=$(PROJECT_ROOT) \
		DEBUG=$(DEBUG)

pl:
	$(MAKE) -C pl all \
		TARGET=$(TARGET) PL=$(PL) \
		PROJECT_ROOT=$(PROJECT_ROOT) \
		DEBUG=$(DEBUG)

$(XSA): $(AIE_LIBADF) $(PL_KERNELS) | link_config
	$(dir_guard)
	v++ --link --target $(TARGET) --platform $(PLATFORM) $(XSA_LINK_OPTS) $^ -o $@

$(XCLBIN): $(XSA) $(AIE_LIBADF)
	$(dir_guard)
	v++ --package --target $(TARGET) --platform $(PLATFORM) $(XCLBIN_PACKAGE_OPTS) $^ -o $@

%emconfig.json:
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(@D)

# Generates the link.cfg file which specifies connections between PL and AIE
.PHONY: link_config
link_config: $(LINK_CONNECTIONS_FILE)

$(LINK_CONNECTIONS_FILE):
	$(dir_guard)
	python3 $(PL_DIR)/generate_link_cfg.py $@ \
		-p $(AIE_PARTIALS_INSTANCES) -d $(AIE_DENSITY_INSTANCES) --aie $(AIE)

# Link backs to trigger PHONY targets if artifacts don't exist
# Those recipes have to have PHONY target so that their respecitve
# makefiles handle the rebuild always. This keeps the dependency
# chain intact.
ifneq ($(AIE),none)
$(AIE_LIBADF): aie
endif
$(PL_KERNELS): pl
$(HOST_EXE): host


.PHONY: clean clean_all
clean:
	$(MAKE) -C aie clean \
		TARGET=$(TARGET) PL=$(PL) AIE=$(AIE) \
		AIE_PARTIALS_INSTANCES=$(AIE_PARTIALS_INSTANCES) \
		AIE_DENSITY_INSTANCES=$(AIE_DENSITY_INSTANCES)
	$(MAKE) -C pl clean \
		TARGET=$(TARGET) PL=$(PL) AIE=$(AIE) \
		AIE_PARTIALS_INSTANCES=$(AIE_PARTIALS_INSTANCES) \
		AIE_DENSITY_INSTANCES=$(AIE_DENSITY_INSTANCES)
	$(MAKE) -C host clean \
		TARGET=$(TARGET) PL=$(PL) AIE=$(AIE) \
		AIE_PARTIALS_INSTANCES=$(AIE_PARTIALS_INSTANCES) \
		AIE_DENSITY_INSTANCES=$(AIE_DENSITY_INSTANCES)

clean_all:
	rm -rf $(BUILD_DIR)

# Scripts to run AIEplace application
#BENCHMARK_DIR = host/benchmarks
#BENCHMARK = ispd2015/mgc_fft_1
#BENCHMARK = ispd2015/mgc_matrix_mult_1
#BENCHMARK = ispd2015/mgc_superblue11_a
#BENCHMARK = ispd2015/mgc_superblue12
#BENCHMARK = ispd2015/mgc_superblue16_a

run:
	@unset XCL_EMULATION_MODE
	@clear
	@echo "make run"
#perf record #use this for perf profiler
	time -p $(HOST_EXE) $(HOST_RUN_CONFIG)

# --- TEMPORARY: build + run the PL HPWL-gradient sw_emu verify on a benchmark ---
# One-shot convenience. Source the toolchain env first:
#   source /tools/Xilinx/Vitis/2022.2/settings64.sh
#   source /opt/xilinx/xrt/setup.sh
#   export PLATFORM_REPO_PATHS=$$HOME/xilinx_local/opt/xilinx/platforms
# Usage (BENCH overridable):
#   make run-hpwl-grad AIE=none PL=pl_algo HOST=pl_algo TARGET=sw_emu BUILD_XRT=1
BENCH ?= $(PROJECT_ROOT)/host/benchmarks/ispd2015/mgc_pci_bridge32_b
.PHONY: run-hpwl-grad
run-hpwl-grad: $(XCLBIN) $(HOST_EXE)
	mkdir -p $(BUILD_DIR)/$(TARGET)/run
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD_DIR)/$(TARGET)/run
	cd $(BUILD_DIR)/$(TARGET)/run && \
	XCL_EMULATION_MODE=$(TARGET) EMCONFIG_PATH=$(BUILD_DIR)/$(TARGET)/run \
	LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_VITIS)/aietools/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
	$(HOST_EXE) --hpwl-grad $(BENCH) $(XCLBIN)

# --- build + run the PL bin-density sw_emu verify on a benchmark ---
# Same toolchain env as run-hpwl-grad. Usage (BENCH overridable):
#   make run-density AIE=none PL=pl_algo HOST=pl_algo TARGET=sw_emu BUILD_XRT=1
.PHONY: run-density
run-density: $(XCLBIN) $(HOST_EXE)
	mkdir -p $(BUILD_DIR)/$(TARGET)/run
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD_DIR)/$(TARGET)/run
	cd $(BUILD_DIR)/$(TARGET)/run && \
	XCL_EMULATION_MODE=$(TARGET) EMCONFIG_PATH=$(BUILD_DIR)/$(TARGET)/run \
	LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_VITIS)/aietools/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
	$(HOST_EXE) --density $(BENCH) $(XCLBIN)

# --- build + run the first AIE-using mode: 1D DCT via the AIE FFT (sw_emu) ---
# AIE-using build (AIE=pl_algo): the libadf is linked into the xsa/xclbin and the
# PL<->AIE stream_connect is emitted (generate_link_cfg.py --aie pl_algo). Usage:
#   make run-dct AIE=pl_algo PL=pl_algo HOST=pl_algo TARGET=sw_emu BUILD_XRT=1
.PHONY: run-dct
run-dct: $(XCLBIN) $(HOST_EXE)
	mkdir -p $(BUILD_DIR)/$(TARGET)/run
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD_DIR)/$(TARGET)/run
	cd $(BUILD_DIR)/$(TARGET)/run && \
	XCL_EMULATION_MODE=$(TARGET) EMCONFIG_PATH=$(BUILD_DIR)/$(TARGET)/run \
	LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_VITIS)/aietools/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
	$(HOST_EXE) --dct $(XCLBIN)

# --- build + run the Stage 3a 8-lane row-DCT pass (sw_emu) ---
# Needs the 8-lane FFT pool, so build with AIE_DENSITY_INSTANCES=8. Usage:
#   make run-dct-rowpass AIE=pl_algo PL=pl_algo HOST=pl_algo TARGET=sw_emu BUILD_XRT=1 AIE_DENSITY_INSTANCES=8
.PHONY: run-dct-rowpass
run-dct-rowpass: $(XCLBIN) $(HOST_EXE)
	mkdir -p $(BUILD_DIR)/$(TARGET)/run
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD_DIR)/$(TARGET)/run
	cd $(BUILD_DIR)/$(TARGET)/run && \
	XCL_EMULATION_MODE=$(TARGET) EMCONFIG_PATH=$(BUILD_DIR)/$(TARGET)/run \
	LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_VITIS)/aietools/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
	$(HOST_EXE) --dct-rowpass $(XCLBIN)

# --- build + run the Stage 3b matrix transpose (naive + tiled), sw_emu ---
# Pure-PL modes, but shares the mode-flag top (AIE ports present), so build the
# AIE-using xclbin. Usage:
#   make run-transpose AIE=pl_algo PL=pl_algo HOST=pl_algo TARGET=sw_emu BUILD_XRT=1 AIE_DENSITY_INSTANCES=8
.PHONY: run-transpose
run-transpose: $(XCLBIN) $(HOST_EXE)
	mkdir -p $(BUILD_DIR)/$(TARGET)/run
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD_DIR)/$(TARGET)/run
	cd $(BUILD_DIR)/$(TARGET)/run && \
	XCL_EMULATION_MODE=$(TARGET) EMCONFIG_PATH=$(BUILD_DIR)/$(TARGET)/run \
	LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_VITIS)/aietools/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
	$(HOST_EXE) --transpose $(XCLBIN)

# --- build + run the Stage 3c FUSED DCT+transpose pass, sw_emu ---
# AIE-using (FFT pool + PL<->AIE streams). Usage:
#   make run-dct-transpose AIE=pl_algo PL=pl_algo HOST=pl_algo TARGET=sw_emu BUILD_XRT=1 AIE_DENSITY_INSTANCES=8
.PHONY: run-dct-transpose
run-dct-transpose: $(XCLBIN) $(HOST_EXE)
	mkdir -p $(BUILD_DIR)/$(TARGET)/run
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD_DIR)/$(TARGET)/run
	cd $(BUILD_DIR)/$(TARGET)/run && \
	XCL_EMULATION_MODE=$(TARGET) EMCONFIG_PATH=$(BUILD_DIR)/$(TARGET)/run \
	LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_VITIS)/aietools/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
	$(HOST_EXE) --dct-transpose $(XCLBIN)


# make dse
# launch python script to perform Design Space Exploration
dse:
	python3 dse.py

runemu:
	@clear
	export XCL_EMULATION_MODE=hw_emu; bin/AIEplace.exe ${BENCHMARK_DIR}/${BENCHMARK} bin/aieplace.hw_emu.xclbin

profile:
	clear;
#	gprof -qcomputeAllPartials_AIE bin/AIEplace.exe gmon.out 
#	gprof -b -qPlacer::computeAllPartials_AIE bin/AIEplace.exe
#	gprof --graph bin/AIEplace.exe
	gprof --annotated-source bin/AIEplace.exe > annotation.txt
# Visualization
#	gprof bin/AIEplace.exe | gprof2dot -s -w | dot -Tpng -o profile.png


cpplines:
	git ls-files | grep '\.cpp' | xargs wc -l

# make reset
# Hot reset VCK5000 device
.PHONY: reset
reset:
	xbutil reset --device