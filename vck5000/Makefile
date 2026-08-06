# Top level Makefile which calls subdirectory Makefiles

PROJECT_ROOT := $(CURDIR)

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
	@echo -e '\tmake pl-algo:\t Shorthand for: make pl PL=pl_algo HOST=pl_algo (HLS C-synth of top.xo).'
	@echo ""
	@echo 'Run and verify:'
	@echo -e '\tmake run:\t Build (if needed) and run HOST=$(HOST) with its default run_config.toml.'
	@echo -e '\tmake test:\t Tier-1 offline verification suite. Seconds, pure g++, no Vitis/XRT needed.'
	@echo -e '\tmake test-regress:\t sw_only regression vs committed baselines (~12 s).'
	@echo -e '\tmake test-regress-slow:\t ...plus the mixed-size phase-2 design (~3 min).'
	@echo ""

.PHONY: all xclbin xsa aie pl host
all: xclbin host
xclbin: $(XCLBIN)
xsa: $(XSA)

# Build the PL-centric pl_algo design without setting PL/HOST by hand. TARGET passes through,
# so bare `make pl-algo` is the HLS C-synthesis of top.xo (TARGET defaults to hw in common.mk).
# Replaces the old `INPUT_FILE=mkcfg` config-include mechanism, removed 2026-08-05: it was a
# directory plus an include to avoid typing two variable assignments, with one config file.
.PHONY: pl-algo
pl-algo:
	$(MAKE) pl PL=pl_algo HOST=pl_algo TARGET=$(TARGET)

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

run: $(HOST_EXE)
	@unset XCL_EMULATION_MODE
	@clear
	@echo "make run"
#perf record #use this for perf profiler
	time -p $(HOST_EXE) $(HOST_RUN_CONFIG)

# =============================================================================
#  make test -- tier-1 verification suite (see test/Makefile)
# =============================================================================
# Pure g++: no Vitis, no XRT, no device, no xclbin, and no toolchain env to source. Runs in
# seconds and exits non-zero if any module has drifted from its golden -- so it is safe and
# cheap to run after every edit. The slower tiers are separate on purpose:
#   tier 2  HLS C-synthesis   test/synth_check.tcl   (needs Vitis)
#   tier 3  sw_emu modes      the run-* targets below   (needs a built xclbin)
TEST_DIR ?= $(PROJECT_ROOT)/test

.PHONY: test
test:
	$(MAKE) -C $(TEST_DIR) test

# =============================================================================
#  make test-regress -- sw_only regression tripwire (see test/regress/README.md)
# =============================================================================
# Runs sw_only on frozen configs and asserts the per-iteration trajectory and the final cell
# positions are BIT-IDENTICAL to committed baselines (deterministic = true makes that exact,
# not a tolerance). Deliberately NOT part of `make test`: this costs tens of seconds, and
# `make test` earns its keep by being cheap enough to run after every edit.
#   make test-regress        the fast designs (~12 s)
#   make test-regress-slow   also the mixed-size design: phase 2 + macro legalization (~3 min)
SW_ONLY_EXE = $(BUILD_DIR)/$(TARGET)/host/sw_only/aieplace_sw_only.exe
REGRESS = AIEPLACE_EXE=$(SW_ONLY_EXE) $(TEST_DIR)/regress/run_regress.sh

.PHONY: test-regress test-regress-slow
test-regress:
	$(MAKE) host HOST=sw_only
	$(REGRESS)

test-regress-slow:
	$(MAKE) host HOST=sw_only
	$(REGRESS) --slow

# =============================================================================
#  pl_algo module verifies: build + run one bring-up mode under emulation.
# =============================================================================
# Every one of these is the same recipe with a different host --flag, so they all go
# through EMU_RUN. Source the toolchain env first:
#   source /tools/Xilinx/Vitis/2022.2/settings64.sh
#   source /opt/xilinx/xrt/setup.sh
#   export PLATFORM_REPO_PATHS=$$HOME/xilinx_local/opt/xilinx/platforms
# The AIE-using targets all reuse the SAME xclbin -- rebuild it once, then swap the target
# (skip the build/sw_emu rm between them if only the host changed).
#
#   $(1) = host args before the xclbin (the --flag, plus $(BENCH) where the mode takes one)
#   $(2) = host args after the xclbin (only run-place, which takes an iteration count)
define EMU_RUN
	mkdir -p $(BUILD_DIR)/$(TARGET)/run
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(BUILD_DIR)/$(TARGET)/run
	cd $(BUILD_DIR)/$(TARGET)/run && \
	XCL_EMULATION_MODE=$(TARGET) EMCONFIG_PATH=$(BUILD_DIR)/$(TARGET)/run \
	LD_LIBRARY_PATH=$(XILINX_VITIS)/lib/lnx64.o:$(XILINX_VITIS)/aietools/lib/lnx64.o:$(XILINX_XRT)/lib:$$LD_LIBRARY_PATH \
	$(HOST_EXE) $(1) $(XCLBIN) $(2)
endef

BENCH ?= $(PROJECT_ROOT)/host/benchmarks/ispd2015/mgc_pci_bridge32_b
PLACE_ITERS ?= 2

# --- PL-only modes (AIE ports are present in the top, but unused) ---
#   make run-hpwl-grad AIE=none PL=pl_algo HOST=pl_algo TARGET=sw_emu BUILD_XRT=1
.PHONY: run-hpwl-grad run-density
run-hpwl-grad: $(XCLBIN) $(HOST_EXE)          # HPWL gradient vs the CPU golden
	$(call EMU_RUN,--hpwl-grad $(BENCH))
run-density: $(XCLBIN) $(HOST_EXE)            # Stage 1: bin density rho
	$(call EMU_RUN,--density $(BENCH))

# --- AIE-using modes: need the FFT pool, so build with AIE_DENSITY_INSTANCES=8 ---
#   make run-<target> AIE=pl_algo PL=pl_algo HOST=pl_algo TARGET=sw_emu BUILD_XRT=1 AIE_DENSITY_INSTANCES=8
.PHONY: run-dct run-dct-rowpass run-transpose run-dct-transpose run-auv \
        run-idct-transpose run-idxst-transpose run-spectral run-field \
        run-force-gather run-density-grad run-iter-update run-metrics run-place
run-dct: $(XCLBIN) $(HOST_EXE)                # Stage 2: 1D DCT via the AIE FFT (first AIE mode)
	$(call EMU_RUN,--dct)
run-dct-rowpass: $(XCLBIN) $(HOST_EXE)        # Stage 3a: 8-lane row-DCT pass
	$(call EMU_RUN,--dct-rowpass)
run-transpose: $(XCLBIN) $(HOST_EXE)          # Stage 3b: N x N transpose (naive + tiled), pure PL
	$(call EMU_RUN,--transpose)
run-dct-transpose: $(XCLBIN) $(HOST_EXE)      # Stage 3c: FUSED DCT + transpose pass
	$(call EMU_RUN,--dct-transpose)
run-auv: $(XCLBIN) $(HOST_EXE)                # Stage 3c: forward 2D DCT (a_uv = two fused passes)
	$(call EMU_RUN,--auv)
run-idct-transpose: $(XCLBIN) $(HOST_EXE)     # Stage 4: inverse field solve
	$(call EMU_RUN,--idct-transpose)
run-idxst-transpose: $(XCLBIN) $(HOST_EXE)
	$(call EMU_RUN,--idxst-transpose)
run-spectral: $(XCLBIN) $(HOST_EXE)
	$(call EMU_RUN,--spectral)
run-field: $(XCLBIN) $(HOST_EXE)
	$(call EMU_RUN,--field)
run-force-gather: $(XCLBIN) $(HOST_EXE)       # Stage 5a: force gather (PL-only, rides the AIE xclbin)
	$(call EMU_RUN,--force-gather)
run-density-grad: $(XCLBIN) $(HOST_EXE)       # Stage 5b: density gradient end-to-end
	$(call EMU_RUN,--density-grad)
run-iter-update: $(XCLBIN) $(HOST_EXE)        # Stage 5c.1/5c.2: one Nesterov step, synthetic
	$(call EMU_RUN,--iter-update)
run-metrics: $(XCLBIN) $(HOST_EXE)            # Stage 5c.4: metrics reduce (HPWL + overflow)
	$(call EMU_RUN,--metrics $(BENCH))
run-place: $(XCLBIN) $(HOST_EXE)              # Stage 5c.5/5c.6: full placement loop, PLACE_ITERS iters
	$(call EMU_RUN,--place $(BENCH),$(PLACE_ITERS))


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