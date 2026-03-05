# Top level Makefile which calls subdirectory Makefiles

# Targets: sw_emu, hw_emu, hw
TARGET ?= hw

AIE ?= mark_v1
PL ?= mark_v1

PROJECT_ROOT := $(CURDIR)

include $(PROJECT_ROOT)/common.mk

.PHONY: help all aie partials density pl repackage host clean

help:
	@echo "*#*# Makefile for vck5000 implementation of AIEplace *#*#"
	@echo You can build the different parts of the application with these commands:
	@echo -e '\t"make aie"':'\t' Build the AIE graph configuration. Output: libadf.a
	@echo -e '\t"make partials"': Build the AIE graph for only partials computation.
	@echo -e '\t"make density"':'\t' Build the AIE graph for only density computation.
	@echo -e '\t"make pl"':'\t' Build HLS kernels for the PL. Output: xilinx objects [.xo] in pl/reference_files_hw
	@echo -e '\t"make repackage"':'\t' Package the previously built PL kernels and libadf.a into finished hardware configuration. Output: bin/aieplace.hw.xclbin
	@echo -e '\t"make host"':'\t' Builds the host code which runs on a host x86 machine.
	@echo -e '\t"make all"':'\t' Builds the the entire application (aie, pl, and host).

all: xclbin host
xclbin: $(XCLBIN)
xsa: $(XSA)

aie:
	$(MAKE) -C aie PROJECT_ROOT=$(PROJECT_ROOT) TARGET=$(TARGET) AIE=$(AIE)

pl:
	$(MAKE) -C pl PROJECT_ROOT=$(PROJECT_ROOT) TARGET=$(TARGET) PL=$(PL)

$(XSA): $(AIE_LIBADF) $(PL_KERNELS) | config
	$(dir_guard)
	v++ --link --target $(TARGET) --platform $(PLATFORM) $(XSA_LINK_OPTS) $^ -o $@

$(XCLBIN): $(XSA) $(AIE_LIBADF)
	$(dir_guard)
	v++ --package --target $(TARGET) --platform $(PLATFORM) $(XCLBIN_PACKAGE_OPTS) $^ -o $@

host:
	$(MAKE) -C host

%emconfig.json:
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(@D)

# Generates the link.cfg file which specifies connections between PL and AIE
.PHONY: config
config:
	python3 $(PL_DIR)/generate_link_cfg.py


.PHONY: clean clean_all
clean:
	$(MAKE) -C aie clean
	$(MAKE) -C pl clean
	$(MAKE) -C host clean

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
	time -p bin/AIEplace.exe


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
reset:
	xbutil reset --device
