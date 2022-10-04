XILNX_TOOLS_LOCATION := /proj/xbuilds/SWIP/2022.1_0420_0327/installs/lin64/Vitis/2022.1
PLATFORM_REPO_PATHS := $(XILNX_TOOLS_LOCATION)/base_platforms
#DSPLIB_ROOT
PLATFORM := ${PLATFORM_REPO_PATHS}/xilinx_vck190_base_202210_1/xilinx_vck190_base_202210_1.xpfm
AIE_SRC_DIR := $(PWD)/src
BUILD_TARGET_DIR := $(PWD)/build
TOP := hpwlGraphTop
TOP_SRC_CPP := $(AIE_SRC_DIR)/$(TOP).cpp

AIE_FLAGS := -include=$(AIE_SRC_DIR)
AIE_FLAGS += -include=$(AIE_SRC_DIR)/kernels
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/include/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/src/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/inc
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/src
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/include/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/tests/aie/common/inc
AIE_FLAGS += -include=/proj/xbuilds/SWIP/2022.1_0420_0327/installs/lin64/Vitis/2022.1/aietools/include/
AIE_FLAGS += --verbose
AIE_FLAGS += --platform=$(PLATFORM)
AIE_FLAGS += --kernel-linting

################################################################
# MAKE RULES
################################################################

aie: ./build/libadf.a

./build/libadf.a: $(AIE_SRC_DIR)/* $(AIE_SRC_DIR)/kernels/*
	mkdir -p $(BUILD_TARGET_DIR); \
	cd $(BUILD_TARGET_DIR); \
	aiecompiler $(AIE_FLAGS) $(TOP_SRC_CPP) 2>&1 | tee -a aiecompiler.log
	@echo ""
	@echo "AIE Compilation Complete..."
	@echo "####################################"
	@echo ""


AIE_SIM_FLAGS := --pkg-dir Work/
AIE_SIM_FLAGS += -i=$(PWD)
#AIE_SIM_FLAGS += --profile

sim: aie
	cd $(BUILD_TARGET_DIR); \
	aiesimulator $(AIE_SIM_FLAGS) 2>&1 | tee -a aiesim.log
	@echo ""
	@echo "AIE Simulation Complete..."
	@echo "####################################"
	@echo ""

analyze:
	vitis_analyzer ./build/Work/$(TOP).aiecompile_summary