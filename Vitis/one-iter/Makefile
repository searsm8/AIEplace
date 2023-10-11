TARGET = sw_emu
#sw_emu, hw, hw_emu

XILNX_TOOLS_LOCATION := /proj/xbuilds/SWIP/2022.1_0420_0327/installs/lin64/Vitis/2022.1
PLATFORM_REPO_PATHS := $(XILNX_TOOLS_LOCATION)/base_platforms

DSPLIB_ROOT := /home/msears/Vitis_Libraries/dsp

PLATFORM = ${XILINX_VITIS}/base_platforms/xilinx_vck190_base_202220_1/xilinx_vck190_base_202220_1.xpfm
SRC_DIR := $(PWD)/src
BUILD_DIR := $(PWD)/build
AIE_DIR := $(BUILD_DIR)#/aie
XSA_DIR := $(BUILD_DIR)#/xsa
PKG_DIR := $(BUILD_DIR)#/pkg

TOP := iter_graph
TOP_SRC_CPP := $(SRC_DIR)/$(TOP).cpp
LIBADF := $(PWD)/libadf.a

AIE_FLAGS := -include=$(SRC_DIR)
AIE_FLAGS += -include=$(SRC_DIR)/kernels
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/include/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/src/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/inc
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L1/tests/aie/src
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/include/aie
AIE_FLAGS += -include=$(DSPLIB_ROOT)/L2/tests/aie/common/inc
AIE_FLAGS += -include=$(XILINX_VITIS)/aietools/include/
AIE_FLAGS += --verbose
AIE_FLAGS += --platform=$(PLATFORM)
AIE_FLAGS += --kernel-linting


################################################################
# MAKE RULES
################################################################

all: aie xsa host pkg


aie: ${LIBADF}

${LIBADF}: $(SRC_DIR)/* $(SRC_DIR)/kernels/*
ifeq ($(TARGET), sw_emu) 
	mkdir -p $(AIE_DIR); cd $(AIE_DIR); \
	aiecompiler $(AIE_FLAGS) --target=x86sim $(TOP_SRC_CPP) 2>&1 | tee -a aiecompiler.log 
else 
	mkdir -p $(AIE_DIR); cd $(AIE_DIR); \
	aiecompiler $(AIE_FLAGS) $(TOP_SRC_CPP) 2>&1 | tee -a aiecompiler.log 
endif
	@echo ""
	@echo "AIE Compilation Complete..."
	@echo "####################################"
	@echo ""
	cp $(AIE_DIR)/libadf.a .


AIE_SIM_FLAGS := --pkg-dir=Work/
AIE_SIM_FLAGS += --input-dir=$(PWD)
#AIE_SIM_FLAGS += --output-dir=$(PWD)
AIE_SIM_FLAGS += --profile
AIE_SIM_FLAGS += --hang-detect-time=10000
AIE_SIM_FLAGS += --dump-vcd AIEplace

sim: aie
ifeq ($(TARGET), sw_emu)
	cd $(BUILD_DIR); \
	x86simulator --pkg-dir=Work/ -i=$(PWD) 2>&1 | tee -a aiesim.log
else
	cd $(BUILD_DIR); \
	aiesimulator $(AIE_SIM_FLAGS) 2>&1 | tee -a aiesim.log; \
	vcdanalyze --vcd=AIEplace.vcd -wdb
endif
	@echo ""
	@echo "AIE Simulation Complete..."
	@echo "####################################"
	@echo ""


XSA = $(PWD)/${TOP}_${TARGET}.xsa
VPP_SPEC =system.cfg
VPP_FLAGS=--save-temps --verbose#--config ${VPP_SPEC}  

xsa: ${XSA}

${XSA}: aie
	mkdir -p $(XSA_DIR); cd $(XSA_DIR); \
	v++ --debug --link --platform ${PLATFORM} ${LIBADF} -t ${TARGET} ${VPP_FLAGS} -o $@

HOST_EXE = $(PWD)/host.exe
CXXFLAGS += -std=c++14 -I$(XILINX_VIVADO)/include/ -I${SDKTARGETSYSROOT}/usr/include/xrt/ -O0 -g -Wall -c -fmessage-length=0 --sysroot=${SDKTARGETSYSROOT} -I${XILINX_VITIS}/aietools/include ${HOST_INC}
GCC_FLAGS := -O
GCC_FLAGS += -c
GCC_FLAGS += -std=c++14 # c++1y for gcc <4.9 ; c++14 for gcc >=4.9
GCC_FLAGS += -D__linux__
GCC_FLAGS += -D__PS_ENABLE_AIE__
GCC_FLAGS += -DXAIE_DEBUG

LDFLAGS := -lxaiengine -ladf_api_xrt -lgcc -lc -lxrt_coreutil -lxilinxopencl -lpthread -lrt -ldl -lcrypt -lstdc++ -L${SDKTARGETSYSROOT}/usr/lib/ --sysroot=${SDKTARGETSYSROOT} -L$(XILINX_VITIS)/aietools/lib/aarch64.o
GCC_LIB := -lxaiengine -ladf_api_xrt -lxrt_core -lxrt_coreutil -L$(SDKTARGETSYSROOT)/usr/lib -L${XILINX_VITIS}/aietools/lib/aarch64.o --sysroot=${SDKTARGETSYSROOT}

GCC_INCLUDES := -I$(XILINX_VITIS)/aietools/include
GCC_INCLUDES += -I$(SRC_DIR)
GCC_INCLUDES += -I$(SRC_DIR)/kernels
GCC_INCLUDES += -I$(SDKTARGETSYSROOT)/usr/include/xrt
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/include/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/src/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/tests/aie/inc
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/tests/aie/src
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L2/include/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L2/tests/aie/common/inc

HOST_OBJ   = aie_control_xrt.o graph.o

host: ${HOST_EXE}

${HOST_EXE}:
	$(CXX) $(GCC_FLAGS) $(GCC_INCLUDES) -o $(BUILD_DIR)/aie_control_xrt.o $(AIE_DIR)/Work/ps/c_rts/aie_control_xrt.cpp;
	$(CXX) $(GCC_FLAGS) $(GCC_INCLUDES) -o $(BUILD_DIR)/$(TOP).o $(TOP_SRC_CPP); 
	$(CXX) $(BUILD_DIR)/*.o $(GCC_LIB) -std=c++14 -o ${HOST_EXE}

pkg: package_${TARGET}

package_${TARGET}: ${LIBADF} ${HOST_EXE} 
	mkdir -p $(PKG_DIR); cd $(PKG_DIR); \
	v++ --package -t ${TARGET} -f ${PLATFORM} \
		--package.rootfs ${ROOTFS} \
		--package.kernel_image ${IMAGE} \
		--package.boot_mode=sd \
		--package.image_format=ext4 \
		--package.sd_file ${HOST_EXE} \
		${XSA} ${LIBADF}
	mv $(PKG_DIR)/sd_card.img .
#--package.defer_aie_run \
# --package.sd_file embedded_exec.sh \

emu:
	cd ${BUILD_DIR}; \
	./launch_hw_emu.sh

analyze:
	vitis_analyzer ./build/Work/$(TOP).aiecompile_summary ./build/system.wdb

clean:
	rm -rf ${HOST_EXE} ${LIBADF} *.xclbin *.xsa* sd_card.img *.jou *.log *.link_summary
	rm -rf _x .Xil build sd_card