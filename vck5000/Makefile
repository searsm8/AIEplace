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
aie: $(AIE_LIBADF)
pl: $(PL_KERNELS)
host: $(HOST_EXE)

$(AIE_LIBADF):
	$(MAKE) -C aie all \
		TARGET=$(TARGET) AIE=$(AIE) \
		PROJECT_ROOT=$(PROJECT_ROOT) \
		DEBUG=$(DEBUG)

%.xo:
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

$(HOST_EXE):
	$(MAKE) -C host PROJECT_ROOT=$(PROJECT_ROOT) HOST=$(HOST)

%emconfig.json:
	emconfigutil --platform $(PLATFORM) --nd 1 --od $(@D)

# Generates the link.cfg file which specifies connections between PL and AIE
.PHONY: link_config
link_config: $(LINK_CONNECTIONS_FILE)

$(LINK_CONNECTIONS_FILE):
	$(dir_guard)
	python3 $(PL_DIR)/generate_link_cfg.py $@ \
		-p $(AIE_PARTIALS_INSTANCES) -d $(AIE_DENSITY_INSTANCES)


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
.PHONY: reset
reset:
	xbutil reset --device
