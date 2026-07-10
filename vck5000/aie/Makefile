# Makefile for AIEplace: Versal implementation of ePlace algorithm
#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PROJECT_ROOT ?= $(CURDIR)/..

include $(PROJECT_ROOT)/common.mk

PLATFORM := /opt/xilinx/platforms/$(PLATFORM)/$(PLATFORM).xpfm

TOPGRAPH := TopGraph
AIE_SRCS_MAIN := $(AIE_DIR)/src/$(TOPGRAPH).cpp
AIE_SRCS_OTHER := $(filter-out $(AIE_SRCS_MAIN), $(wildcard $(AIE_DIR)/src/*))

AIE_FLAGS := --include "$(AIE_DIR)/src"
AIE_FLAGS += --Xpreproc="-DPARTIALS_INSTANCES=$(AIE_PARTIALS_INSTANCES) -DDENSITY_CHANNELS=$(AIE_DENSITY_INSTANCES)"
AIE_FLAGS += --stacksize=2048

# Add subcomponent variables
-include $(AIE_DIR)/makeflags.mk

ifeq ($(DEBUG),1)
	AIE_FLAGS += --verbose
	AIE_FLAGS += --kernel-linting# Used for debugging, has overhead!
	AIE_FLAGS += --event-trace=runtime
	AIE_FLAGS += -xlopt=1
endif

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
	aiecompiler --target=${AIE_TARGET} --platform=$(PLATFORM) ${AIE_FLAGS} --workdir=$(AIE_WORKDIR) $< --output-archive $@

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
.PHONY: aiesim x86sim
# Simulate AIE code
aiesim:  $(BUILD_DIR)/hw/aie/$(AIE)/libadf_$(AIE)_$(INSTANCES_CONFIG).a
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running aiesimulator..."
	aiesimulator --pkg-dir=$(AIE_WORKDIR) --profile --dump-vcd=AIEplace --hang-detect-time=10000 --output-dir=$(BUILD_DIR)/x86simulator_output

x86sim:  $(BUILD_DIR)/x86sim/aie/libadf_$(AIE).a
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO: Running x86simulator..."
	x86simulator --pkg-dir=$(AIE_WORKDIR) --output-dir=$(BUILD_DIR)/x86simulator_output

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Analysis
.PHONY: vcdanalyze analyze
# make vcdanalyze processes the trace, but this can also be done in the analyzer GUI
vcdanalyze: system.wdb

system.wdb:
	vcdanalyze --vcd=AIEplace.vcd -wdb

analyze: #system.wdb
	vitis_analyzer $(AIE_WORKDIR)/$(TOPGRAPH).aiecompile_summary

.PHONY: browse
browse:
	$(PROJECT_ROOT)/aie/scripts/browse_aie.sh $(AIE_WORKDIR)/aie

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#Clean build products
.PHONY: clean clean_all clean_x86sim clean_hw clean_sim
clean:
	rm -rf $(AIE_WORKDIR) $(AIE_LIBADF)

clean_all: clean_x86sim clean_hw clean_sim

clean_x86sim:
	rm -rf $(BUILD_DIR)/x86sim/aie
clean_hw:
	rm -rf $(BUILD_DIR)/hw/aie

clean_sim:
	rm -rf .Xil .ipcache vivado* *.xpe *.txt *.log *.wcfg *.wdb *.vcd
	rm -rf xnwOut .AIE_SIM_CMD_LINE_OPTIONS pl_sample_count* *.html ISS_RPC_SERVER_PORT vitis_analyzer*
	rm -rf $(BUILD_DIR)/x86simulator_output $(BUILD_DIR)/aiesimulator_output

