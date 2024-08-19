# Top level Makefile which calls subdirectory Makefiles

# Modify these parameters to determine what is built
TARGET ?= hw# choose: hw, hw_emu, or sw_emu
AIE_GRAPH = aie# choose: aie, partials, or density

.PHONY: help all aie partials density pl repackage host clean

help:
	@echo *#*# Makefile for vck5000 implementation of AIEplace *#*#
	@echo You can build the different parts of the application with these commands:
	@echo -e '\t"make aie"':'\t' Build the AIE graph configuration. Output: libadf.a
	@echo -e '\t"make partials"': Build the AIE graph for only partials computation.
	@echo -e '\t"make density"':'\t' Build the AIE graph for only density computation.
	@echo -e '\t"make pl"':'\t' Build HLS kernels for the PL. Output: xilinx objects [.xo] in pl/reference_files_hw
	@echo -e '\t"make repackage"':'\t' Package the previously built PL kernels and libadf.a into finished hardware configuration. Output: bin/aieplace.hw.xclbin
	@echo -e '\t"make host"':'\t' Builds the host code which runs on a host x86 machine.
	@echo -e '\t"make all"':'\t' Builds the the entire application (aie, pl, and host).

all: $(AIE_GRAPH) pl host

aie:
	$(MAKE) -C aie $(AIE_GRAPH)

partials:
	$(MAKE) -C aie partials

density:
	$(MAKE) -C aie density

pl:
	$(MAKE) -C pl pl TARGET=$(TARGET)

repackage:
	$(MAKE) -C pl repackage TARGET=$(TARGET)

host:
	$(MAKE) -C host

clean:
	$(MAKE) -C aie clean
	$(MAKE) -C pl clean
	$(MAKE) -C host clean

# Scripts to run AIEplace application
BENCHMARK_DIR = host/benchmarks

BENCHMARK = ispd2015/mgc_fft_1
#BENCHMARK = ispd2015/mgc_matrix_mult_1/
#BENCHMARK = ispd2015/mgc_superblue11_a/
#BENCHMARK = ispd2015/mgc_superblue12/
#BENCHMARK = ispd2015/mgc_superblue16_a/

runhw:
	@unset XCL_EMULATION_MODE
	@clear
	@echo "runhw: BENCHMARK =" ${BENCHMARK}
#perf record #use this for perf profiler
	time bin/AIEplace.exe ${BENCHMARK_DIR}/${BENCHMARK} bin/aieplace.hw.xclbin

runemu:
	@clear
	export XCL_EMULATION_MODE=hw_emu; bin/AIEplace.exe ${BENCHMARK_DIR}/${BENCHMARK} bin/aieplace.hw_emu.xclbin

profile:
	clear;
#	gprof -qcomputeAllPartials_AIE bin/AIEplace.exe gmon.out 
#	gprof -b -qPlacer::computeAllPartials_AIE bin/AIEplace.exe
#	gprof --graph bin/AIEplace.exe
#	gprof --annotated-source bin/AIEplace.exe > annotation.txt
# Visualization
	gprof bin/AIEplace.exe | gprof2dot -s -w | dot -Tpng -o profile.png
