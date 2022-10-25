
TARGET := hw_emu #sw_emu, hw_emu, or hw
TIME := time # leave blank to not time commands

ROOTFS ?= /home/msears/tools/xilinx-versal-common-v2022.1/rootfs.ext4
IMAGE ?=  /home/msears/tools/xilinx-versal-common-v2022.1/Image

XILNX_TOOLS_LOCATION := /proj/xbuilds/SWIP/2022.1_0420_0327/installs/lin64/Vitis/2022.1
PLATFORM_REPO_PATHS := $(XILNX_TOOLS_LOCATION)/base_platforms

#DSPLIB_ROOT :=
PLATFORM := ${PLATFORM_REPO_PATHS}/xilinx_vck190_base_202210_1/xilinx_vck190_base_202210_1.xpfm
SRC_DIR := $(PWD)/src
AIE_SRC_DIR := $(SRC_DIR)/aie_src
PL_SRC_DIR  := $(SRC_DIR)/pl_src
PS_SRC_DIR  := $(SRC_DIR)/ps_src
BUILD_DIR := $(PWD)/build

TOP := AIEplaceGraph
AIE_TOP_CPP := $(AIE_SRC_DIR)/$(TOP).cpp
XILINX_OBJECTS := mm2s.xo transpose.xo s2mm_transpose.xo
CONFIG_FILE := $(PWD)/system.cfg

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
.PHONY: aie aiengine_graph pl pl_kernels sim xsa ps application pkg package

all: aie pl sim xsa ps package

# =========================================================
# Step 1. AI Engine SDF Graph File and $(WORK_DIR)/ Directory
#         (containing the Graph Executable) Generation
# ========================================================
# Compiles the aie kernels and graphs
# Output: .build/libadf.a
# make aie

aie: aiengine_graph 

aiengine_graph: ./libadf.a

./libadf.a: $(AIE_SRC_DIR)/* $(AIE_SRC_DIR)/kernels/*
	@echo "####################################"
	@echo "BEGIN: make aie"
	@echo "####################################"
	@mkdir -p $(BUILD_DIR);
	@mkdir -p $(BUILD_DIR)/out; 
	$(TIME) aiecompiler $(AIE_INCLUDES) $(AIE_FLAGS) $(AIE_TOP_CPP) --output-archive="./libadf.a" 2>&1 | tee -a aiecompiler.log
	@echo "####################################"
	@echo "END: make aie"
	@echo "####################################"
	@echo ""

# =========================================================
# Step 2. Kernel XO File Generation
# ========================================================
# This step compiles the HLS C PL kernels.
# make pl

PL_FLAGS := -t $(TARGET)
PL_FLAGS += --platform $(PLATFORM)
PL_FLAGS += --save-temps -g

pl: pl_kernels # alias

pl_kernels: $(XILINX_OBJECTS)

%.xo: $(PL_SRC_DIR)/%.cpp
	@echo "v++ --compile --kernel $(basename $(notdir $@)) ..."
	$(TIME) v++ --compile --kernel $(basename $(notdir $@)) $(PL_FLAGS) $< -o $@

SIM_FLAGS := --pkg-dir=Work/
SIM_FLAGS += -i=$(PWD)
# is profile option needed if you plan to run hw emulation later?
SIM_FLAGS += --profile 
#SIM_FLAGS += --dump-vcd=$(TOP) # BEWARE: creating a VCD can eat a TON of disk space for large sims!

# Run the AIE simulator, reading ./data/input.dat files.
# Output to ./build/aiesimulator_output/data
sim: simulate # alias

simulate: aie
	@echo ""
	@echo "####################################"
	@echo "BEGIN: AIE Simulation"
	@echo "####################################"
	@echo TARGET=$(TARGET)
ifeq ($(TARGET),sw_emu)
	$(TIME) x86simulator $(SIM_FLAGS)  2>&1 | tee -a x86sim.log
else
	$(TIME) aiesimulator $(SIM_FLAGS) > aiesim.log
endif 
	@echo "####################################"
	@echo "END: AIE Simulation"
	@echo "####################################"
	@echo ""


emu: emulate # alias

emulate:
	./launch_hw_emu.sh -aie-sim-options ./aiesimulator_output/aiesim_options.txt -add-env AIE_COMPILER_WORKDIR=./Work 

analyze: analyze_aie # alias

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

VPP_LINK_FLAGS := --target $(TARGET) --platform $(PLATFORM) libadf.a $(XILINX_OBJECTS) --save-temps -g --config $(CONFIG_FILE) -o AIEplace.xsa
#VPP_LINK_FLAGS := -l -t $(TARGET) --platform $(BASE_PLATFORM) $(KERNEL_XO) $(GRAPH_O)  --save-temps -g --config $(CONFIG_FILE) -o $(PFM).xsa

# Link Xilinx Object Files (.xo) into a .xsa file
# Link the platform, PL kernels, and AIE graph
xsa: $(CONFIG_FILE)
	@echo "####################################"
	@echo "BEGIN: make xsa"
	@echo "####################################"
	@echo v++ --link ...
	v++ --link $(VPP_LINK_FLAGS)
	@echo "####################################"
	@echo "END: make xsa"
	@echo "####################################"
	@echo ""

APP_ELF := fft_2d_aie_xrt.elf
CXX := /tools/batonroot/rodin/devkits/lnx64/gcc-8.3.0/bin/g++

GCC_FLAGS := -O
GCC_FLAGS += -c
GCC_FLAGS += -std=c++14 # c++1y for gcc <4.9 ; c++14 for gcc >=4.9
GCC_FLAGS += -D__linux__
GCC_FLAGS += -D__PS_ENABLE_AIE__
GCC_FLAGS += -DXAIE_DEBUG

REG_GCC_FLAGS := $(GCC_FLAGS)
ITER_CNT := 1
REG_GCC_FLAGS += -DITER_CNT=$(ITER_CNT)


# FOR REFERENCE ONLY
# Compile the host application
#app: aie $(BUILD_DIR)/$(APP_ELF)
#
#
#$(BUILD_DIR)/$(APP_ELF): $(HOST_APP_SRC)/* aie
#	@rm -rf $(BUILD_DIR)/app_control.o $(BUILD_DIR)/fft_2d_aie_app.o $(BUILD_DIR)/$(APP_ELF)
#	$(CXX) $(REG_GCC_FLAGS) $(GCC_INC_FLAGS) $(AIE_CONTROL_CPP) -o $(BUILD_DIR)/app_control.o
#	$(CXX) $(REG_GCC_FLAGS) $(GCC_INC_FLAGS) $(APP_SRC_CPP) -o $(BUILD_DIR)/fft_2d_aie_app.o $(GCC_INC_LIB) $(GCC_LIB)
#	$(CXX) $(BUILD_DIR)/app_control.o $(BUILD_DIR)/fft_2d_aie_app.o $(GCC_INC_LIB) $(GCC_LIB) -o $(BUILD_DIR)/$(APP_ELF)
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
GCC_INCLUDES += -I$(PWD)/src/aie_src
GCC_INCLUDES += -I$(SDKTARGETSYSROOT)/usr/include/xrt
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/include/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/src/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/tests/aie/inc
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/tests/aie/src
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L2/include/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L2/tests/aie/common/inc
CXX := aarch64-xilinx-linux-g++  -mcpu=cortex-a72.cortex-a53 -march=armv8-a+crc -fstack-protector-strong  -O2 -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security --sysroot=${SDKTARGETSYSROOT}
GCC_LIB := -lxaiengine -ladf_api_xrt -lxrt_core -lxrt_coreutil -L$(SDKTARGETSYSROOT)/usr/lib -L${XILINX_VITIS}/aietools/lib/aarch64.o --sysroot=${SDKTARGETSYSROOT}

# Compile the host program to run on the PS
ps: application # alias

application: $(PS_SRC_DIR)/host.cpp
	@echo "Begin PS host app compilation..."
	@cd $(PS_SRC_DIR); \
	$(CXX) $(GCC_FLAGS) $(GCC_INCLUDES) -o aie_control_xrt.o ../../Work/ps/c_rts/aie_control_xrt.cpp; \
	$(CXX) $(GCC_FLAGS) $(GCC_INCLUDES) -o host.o host.cpp; \
	$(CXX) *.o $(GCC_LIB) -std=c++14 -o $(PWD)/host.exe 
	@echo "####################################"
	@echo "END: make ps"
	@echo "####################################"

##################################################################################################
# Depending on the TARGET, it'll either generate the PDI for sw_emu,hw_emu or hw.
pkg: package # alias
#--package.sd_file embedded_exec.sh 
package: #guard-PLATFORM_REPO_PATHS guard-IMAGE guard-ROOTFS
	@echo "\n####################################"
	@echo v++ --package ...
	$(TIME) v++ \
		--package \
		--target ${TARGET} \
		--platform ${PLATFORM} \
		--package.rootfs=${ROOTFS} \
		--package.image_format=ext4 \
		--package.boot_mode=sd \
		--package.kernel_image=${IMAGE} \
		--package.defer_aie_run \
		--package.sd_file host.exe AIEplace.xsa libadf.a 
	@echo "####################################"
	@echo "END: make package."
	@echo "####################################"
	@echo ""
	

###################################################################################################

clean:
	rm -rf $(BUILD_DIR) _x .Xil emu_qemu_scripts sd_card sim aiesimulator_output
	rm -rf *.xo* *.xsa* *.a *.exe *.o *.bin *.BIN *xclbin *.bif *.log *summary *img qemu* *.wcfg *.wdb *.txt