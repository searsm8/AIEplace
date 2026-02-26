# Makefile for AIEplace: Versal implementation of ePlace algorithm
#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PROJECT_ROOT ?= $(CURDIR)/..
PLATFORM := /opt/xilinx/platforms/xilinx_vck5000_gen4x8_qdma_2_202220_1/xilinx_vck5000_gen4x8_qdma_2_202220_1.xpfm

BUILD_DIR := $(PROJECT_ROOT)/build
BUILD_DIR_AIE := $(BUILD_DIR)/$(TARGET)/aie
AIE_LIBADF := $(BUILD_DIR_AIE)/libadf_$(AIE).a

AIE_DIR := $(CURDIR)/src/$(AIE)

TOP := TopGraph
AIE_SRCS_MAIN := $(DIR_AIE)/src/$(AIE)/$(TOPGRAPH).cpp
AIE_SRCS_OTHER := $(filter-out $(AIE_SRCS_MAIN), $(wildcard $(AIE_DIR)/*))

ifeq ($(TARGET),sw_emu)
    TARGET := x86sim
else
    TARGET := hw
endif

AIE_FLAGS := -I "$(AIE_DIR)/src"

# Add subcomponent variables
-include $(AIE_DIR)/makeflags.mk

# DEBUG vars (TODO: add ifdef DEBUG around it)
AIE_FLAGS += --verbose
AIE_FLAGS += --kernel-linting# Used for debugging, has overhead!
AIE_FLAGS += --event-trace=runtime
AIE_FLAGS += -xlopt=1
AIE_FLAGS += --stacksize=2048

# to specify constraints such as location or placement bounding box.
#AIE_FLAGS += --constraints=constraints.json

# Ensures the target recipe dirs exist
dir_guard = @mkdir -p $(@D)

.PHONY: all
all: $(AIE_LIBADF)

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Compile a full graph for AIEplace application, including multiple partials graphs and density graph
$(AIE_LIBADF): $(AIE_SRCS_MAIN) $(AIE_SRCS_OTHER)
	$(dir_guard)
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running aiecompiler for all AIEplace graphs and kernels..."
	aiecompiler --target=${TARGET} --platform=$(PLATFORM) ${AIE_FLAGS} --workdir=$(@D)/Work_$(AIE) $< --output-archive $@

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
.PHONY: aiesim x86sim
# Simulate AIE code
aiesim:  $(BUILD_DIR)/hw/aie/libadf_$(AIE).a
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running aiesimulator..."
	aiesimulator --pkg-dir=$(<D)/Work_$(AIE) --profile --dump-vcd=AIEplace --hang-detect-time=10000 --output-dir=$(BUILD_DIR)/x86simulator_output

x86sim:  $(BUILD_DIR)/x86sim/aie/libadf_$(AIE).a
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running x86simulator..."
	x86simulator --pkg-dir=$(<D)/Work_$(AIE) --output-dir=$(BUILD_DIR)/x86simulator_output

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Analysis
.PHONY: vcdanalyze analyze
# make vcdanalyze processes the trace, but this can also be done in the analyzer GUI
vcdanalyze: system.wdb

system.wdb:
	vcdanalyze --vcd=AIEplace.vcd -wdb

analyze: #system.wdb
	vitis_analyzer $(BUILD_DIR_AIE)/Work_$(AIE)/$(TOP).aiecompile_summary
#vitis_analyzer aiesimulator_output/default.aierun_summary


#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#Clean build products
.PHONY: clean clean_x86sim clean_hw clean_sim

clean: clean_x86sim clean_hw clean_sim

clean_x86sim:
	rm -rf $(BUILD_DIR)/x86sim/aie
clean_hw:
	rm -rf $(BUILD_DIR)/hw/aie

clean_sim:
	rm -rf .Xil .ipcache vivado* *.xpe *.txt *.log *.wcfg *.wdb *.vcd
	rm -rf xnwOut .AIE_SIM_CMD_LINE_OPTIONS pl_sample_count* *.html ISS_RPC_SERVER_PORT vitis_analyzer*
	rm -rf $(BUILD_DIR)/x86simulator_output $(BUILD_DIR)/aiesimulator_output

