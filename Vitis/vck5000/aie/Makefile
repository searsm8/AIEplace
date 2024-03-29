# Makefile for AIEplace: Versal implementation of ePlace algorithm
#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
all: aie aiesim
all_x86: aie_x86 aiesim_x86

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#Clean build products
clean: cleansim
	rm -rf Work libadf.a

cleansim:
	rm -rf .Xil .ipcache vivado* *.xpe *.txt *.log *.wcfg *.wdb *.vcd
	rm -rf x86simulator_output aiesimulator_output xnwOut .AIE_SIM_CMD_LINE_OPTIONS pl_sample_count* *.html ISS_RPC_SERVER_PORT vitis_analyzer*

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#Compile AIE code
aie: libadf.a

TOP := partials/hpwlGraph
GRAPH_TOP := src/${TOP}.cpp


DSPLIB_ROOT := ${HOME}/Vitis_Libraries/dsp# Location of Vitis_Libraries/dsp
XILINX_VITIS := /home/nx08/shared/fpga/xilinx/2022.1/Vitis/2022.1

AIE_FLAGS := -include=src
AIE_FLAGS += -include=src/partials
AIE_FLAGS += -include=src/density
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/include/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/src/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/inc
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/src
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/include/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/tests/aie/common/inc
#AIE_FLAGS += -include=$(XILINX_VITIS)/aietools/include/
AIE_FLAGS += --verbose
#AIE_FLAGS += --platform=$(PLATFORM) # platform not needed for VCK5000?
AIE_FLAGS += --kernel-linting

TARGET ?= hw#choose: hw_emu, hw, sw_emu

# Compile a full graph for AIEplace application, including multiple partials graphs and density graph
libadf.a: src/*
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running aiecompiler for all AIEplace graphs and kernels..."
	@rm -rf Work libadf.a
	@mkdir -p Work
	aiecompiler --target=${TARGET} ${AIE_FLAGS} --workdir=./Work src/TopGraph.cpp

# Compile a graph for only partials computation
partials: src/partials/*
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running aiecompiler for partials..."
	@rm -rf Work libadf.a
	@mkdir -p Work
	aiecompiler --target=${TARGET} ${AIE_FLAGS} --workdir=./Work src/partials/PartialsGraph.cpp

# Compile a graph for only density computation
density: src/density/*
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running aiecompiler for density..."
	@rm -rf Work libadf.a
	@mkdir -p Work
	aiecompiler --target=${TARGET} ${AIE_FLAGS} --workdir=./Work src/density/DCTGraph.cpp



aie_x86: 
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running aiecompiler for x86sim..."
	@rm -rf Work libadf.a
	@mkdir -p Work
	aiecompiler --target=x86sim --include="src" --workdir=./Work ${GRAPH_TOP}

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Simulate AIE code
aiesim: cleansim
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running aiesimulator..."
	aiesimulator --pkg-dir=./Work --profile --dump-vcd AIEplace --hang-detect-time=10000

aiesim_x86: cleansim
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running x86simulator..."
	x86simulator --pkg-dir=./Work


#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Analysis
#.PHONY: vcdanalyze system.wdb
# make vcdanalyze processes the trace, but this can also be done in the analyzer GUI
vcdanalyze: system.wdb

system.wdb:
	vcdanalyze --vcd=AIEplace.vcd -wdb

analyze: #system.wdb
	vitis_analyzer aiesimulator_output/default.aierun_summary
#vitis_analyzer ./Work/$(TOP).aiecompile_summary ./system.wdb


