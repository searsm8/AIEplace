
TARGET := hw_emu#sw_emu, hw_emu, or hw

XILNX_TOOLS_LOCATION := /proj/xbuilds/SWIP/2022.1_0420_0327/installs/lin64/Vitis/2022.1
PLATFORM_REPO_PATHS := $(XILNX_TOOLS_LOCATION)/base_platforms

#DSPLIB_ROOT :=
PLATFORM := ${PLATFORM_REPO_PATHS}/xilinx_vck190_base_202210_1/xilinx_vck190_base_202210_1.xpfm
SRC_DIR := $(PWD)/src
AIE_SRC_DIR := $(SRC_DIR)/aie_src
PL_SRC_DIR  := $(SRC_DIR)/pl_src
PS_SRC_DIR  := $(SRC_DIR)/ps_src
BUILD_TARGET_DIR := $(PWD)/build

TOP := AIEplaceGraph
AIE_TOP_CPP := $(AIE_SRC_DIR)/$(TOP).cpp
PL_KERNELS := $(PL_SRC_DIR)/transpose.cpp
XILINX_OBJECTS := transpose.xo

AIE_INCLUDES := -include=$(AIE_SRC_DIR)
AIE_INCLUDES += -include=$(AIE_SRC_DIR)/kernels
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L1/include/aie
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L1/src/aie
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L1/tests/aie/inc
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L1/tests/aie/src
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L2/include/aie
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L2/tests/aie/common/inc
AIE_INCLUDES += -include=/proj/xbuilds/SWIP/2022.1_0420_0327/installs/lin64/Vitis/2022.1/aietools/include/

AIE_FLAGS := --verbose
AIE_FLAGS += --platform=$(PLATFORM)
AIE_FLAGS += --kernel-linting
ifeq ($(TARGET),sw_emu)
	AIE_FLAGS += --target=x86sim
else
	AIE_FLAGS += --target=hw
endif 


# FLAGS += -include=/proj/xbuilds/XRT/2022.1/202210.2.13.466/packages/x86_64/xrt-2.13.466_centos_7.8/opt/xilinx/xrt

################################################################
# MAKE RULES
################################################################


PL_FLAGS := -t $(TARGET)
PL_FLAGS += --platform $(PLATFORM)
PL_FLAGS += --save-temps -g

# =========================================================
# Step 1. Kernel XO File Generation
# ========================================================
# This step compiles the HLS C PL kernels.
# make pl
pl: $(XILINX_OBJECTS)

%.xo: $(PL_SRC_DIR)/%.cpp
	cd $(BUILD_TARGET_DIR); \
	v++ --compile -k $(basename $(notdir $@)) $(PL_FLAGS) $< -o $@


# =========================================================
# Step 2. AI Engine SDF Graph File and $(WORK_DIR)/ Directory
#         (containing the Graph Executable) Generation
# ========================================================
# Compiles the aie kernels and graphs
# Output: .build/libadf.a
# make aie
aie: ./build/libadf.a

./build/libadf.a: $(AIE_SRC_DIR)/* $(AIE_SRC_DIR)/kernels/*
	mkdir -p $(BUILD_TARGET_DIR); \
	cd $(BUILD_TARGET_DIR); \
	aiecompiler $(AIE_INCLUDES) $(AIE_FLAGS) $(AIE_TOP_CPP) 2>&1 | tee -a aiecompiler.log
	@echo ""
	@echo "AIE Compilation Complete..."
	@echo "####################################"
	@echo ""


SIM_FLAGS := --pkg-dir=Work/
SIM_FLAGS += -i=$(PWD)
#SIM_FLAGS += --profile
#SIM_FLAGS += --dump-vcd=$(TOP) # BEWARE: creating a VCD can eat a TON of disk space for large sims!

	

# Run the AIE simulator, reading ./data/input.dat files.
# Output to ./build/aiesimulator_output/data
sim: aie
	@echo TARGET=$(TARGET)
ifeq ($(TARGET),sw_emu)
	cd $(BUILD_TARGET_DIR); \
	time x86simulator $(SIM_FLAGS)  2>&1 | tee -a x86sim.log
else
	cd $(BUILD_TARGET_DIR); \
	time aiesimulator $(SIM_FLAGS)  2>&1 | tee -a aiesim.log
endif 
	@echo ""
	@echo "####################################"
	@echo "AIE Simulation Complete."
	@echo "####################################"

aiesim: aie
	cd $(BUILD_TARGET_DIR); \
	time aiesimulator $(SIM_FLAGS)  2>&1 | tee -a aiesim.log
	@echo ""
	@echo "####################################"
	@echo "AIE Simulation, With Profiling, Complete..."
	@echo "####################################"

analyze: analyze_aie

# after running aie sim, waveform can be generated using vcd
vcd:
	vcdanalyze -vcd build/$(TOP).vcd 
# -wdb

analyze_aie:
	vitis_analyzer ./build/Work/$(TOP).aiecompile_summary

analyze_link:
	vitis_analyzer AIEplace.xsa.link_summary
	
analyze_sim:
	vitis_analyzer ./build/aiesimulator_output/default.aierun_summary

VPP_LINK_FLAGS := -t $(TARGET) --platform $(PLATFORM) build/libadf.a $(XILINX_OBJECTS) --save-temps -g -o AIEplace.xsa
#VPP_LINK_FLAGS := -l -t $(TARGET) --platform $(BASE_PLATFORM) $(KERNEL_XO) $(GRAPH_O)  --save-temps -g --config $(CONFIG_FILE) -o $(PFM).xsa

# Link Xilinx Object Files (.xo) into a .xsa file
# Link the platform, PL kernels, and AIE graph
xsa: aie pl
	cd $(BUILD_TARGET_DIR); \
	v++ --link $(VPP_LINK_FLAGS)
	@echo "END: make xsa"

APP_ELF := fft_2d_aie_xrt.elf
CXX := /tools/batonroot/rodin/devkits/lnx64/gcc-8.3.0/bin/g++

GCC_FLAGS := -O
GCC_FLAGS += -c
GCC_FLAGS += -std=c++1y # c++1y for gcc <4.9 ; c++14 for gcc >=4.9
GCC_FLAGS += -D__linux__
GCC_FLAGS += -D__PS_ENABLE_AIE__
GCC_FLAGS += -DXAIE_DEBUG

REG_GCC_FLAGS := $(GCC_FLAGS)
ITER_CNT := 1
REG_GCC_FLAGS += -DITER_CNT=$(ITER_CNT)


# FOR REFERENCE ONLY
# Compile the host application
#app: aie $(BUILD_TARGET_DIR)/$(APP_ELF)
#
#
#$(BUILD_TARGET_DIR)/$(APP_ELF): $(HOST_APP_SRC)/* aie
#	@rm -rf $(BUILD_TARGET_DIR)/app_control.o $(BUILD_TARGET_DIR)/fft_2d_aie_app.o $(BUILD_TARGET_DIR)/$(APP_ELF)
#	$(CXX) $(REG_GCC_FLAGS) $(GCC_INC_FLAGS) $(AIE_CONTROL_CPP) -o $(BUILD_TARGET_DIR)/app_control.o
#	$(CXX) $(REG_GCC_FLAGS) $(GCC_INC_FLAGS) $(APP_SRC_CPP) -o $(BUILD_TARGET_DIR)/fft_2d_aie_app.o $(GCC_INC_LIB) $(GCC_LIB)
#	$(CXX) $(BUILD_TARGET_DIR)/app_control.o $(BUILD_TARGET_DIR)/fft_2d_aie_app.o $(GCC_INC_LIB) $(GCC_LIB) -o $(BUILD_TARGET_DIR)/$(APP_ELF)
#	@echo "####################################"
#	@echo "END: make app"
#	@echo "####################################"


# Is this a useable XRT?
#source /proj/xbuilds/2022.1_released/xbb/xrt/packages/setenv.sh
#

# Is this a useable XRT?
XRT_PATH := /proj/xbuilds/XRT/2022.1/202210.2.13.466/packages/x86_64/xrt-2.13.466_centos_7.8/opt/xilinx/xrt/include/
SDKTARGETSYSROOT := /home/msears/tools/SDK/sysroots/cortexa72-cortexa53-xilinx-linux
GCC_INCLUDES := -I$(XILINX_VITIS)/aietools/include
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/include/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/src/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/tests/aie/inc
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/tests/aie/src
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L2/include/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L2/tests/aie/common/inc
CXX := aarch64-xilinx-linux-g++  -mcpu=cortex-a72.cortex-a53 -march=armv8-a+crc -fstack-protector-strong  -O2 -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security --sysroot=${SDKTARGETSYSROOT}
GCC_LIB := -lxaiengine -ladf_api_xrt -lxrt_core -lxrt_coreutil -L$(SDKTARGETSYSROOT)/usr/lib -L${XILINX_VITIS}/aietools/lib/aarch64.o --sysroot=${SDKTARGETSYSROOT}

# Compile the host program to run on the PS
ps:
	cd ./src/ps_src/; \
	$(CXX) $(GCC_FLAGS) $(GCC_INCLUDES) -o aie_control_xrt.o ../../build/Work/ps/c_rts/aie_control_xrt.cpp; \
	$(CXX) $(GCC_FLAGS) $(GCC_INCLUDES) -o host.o host.cpp; \
	$(CXX) *.o $(GCC_LIB) -std=c++1y -o host.exe 
	@echo "####################################"
	@echo "END: make ps"
	@echo "####################################"


clean:
	cd $(BUILD_TARGET_DIR); \
	rm -rf *.log *.str *.jou *summary *.xo *.xsa libadf.a