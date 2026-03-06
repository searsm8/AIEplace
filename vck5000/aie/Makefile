# Makefile for AIEplace: Versal implementation of ePlace algorithm
#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PROJECT_ROOT ?= $(CURDIR)/..
TARGET ?= hw

ifeq ($(TARGET),sw_emu)
    TARGET := x86sim
else
    TARGET := hw
endif

include $(PROJECT_ROOT)/common.mk

PLATFORM := /opt/xilinx/platforms/$(PLATFORM)/$(PLATFORM).xpfm

TOPGRAPH := TopGraph
AIE_SRCS_MAIN := $(AIE_DIR)/src/$(TOPGRAPH).cpp
AIE_SRCS_OTHER := $(filter-out $(AIE_SRCS_MAIN), $(wildcard $(AIE_DIR)/src/*))

AIE_FLAGS := --include "$(AIE_DIR)/src"

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
	vitis_analyzer $(BUILD_DIR_AIE)/Work_$(AIE)/$(TOPGRAPH).aiecompile_summary
#vitis_analyzer aiesimulator_output/default.aierun_summary

browse:
	$(PROJECT_ROOT)/aie/scripts/browse_aie.sh $(PROJECT_ROOT)/build/$(TARGET)/aie/Work_$(AIE)/aie

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

