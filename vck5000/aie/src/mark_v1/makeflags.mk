
# Location of Vitis_Libraries/dsp
DSPLIB_ROOT := $(PROJECT_ROOT)/aie/lib/Vitis_Libraries/dsp

#XILINX_VITIS := /home/nx08/shared/fpga/xilinx/2022.1/Vitis/2022.1

#AIE_FLAGS += -include=src/partials
#AIE_FLAGS += -include=src/density
AIE_FLAGS += -include="$(XILINX_VITIS)/aietools/include/"

AIE_FLAGS += -include="$(PROJECT_ROOT)/host/src/include"

AIE_FLAGS += -include="$(DSPLIB_ROOT)/L1/include/aie"
AIE_FLAGS += -include="$(DSPLIB_ROOT)/L1/src/aie"
AIE_FLAGS += -include="$(DSPLIB_ROOT)/L1/tests/aie/inc"
AIE_FLAGS += -include="$(DSPLIB_ROOT)/L1/tests/aie/src"
AIE_FLAGS += -include="$(DSPLIB_ROOT)/L2/include/aie"
AIE_FLAGS += -include="$(DSPLIB_ROOT)/L2/tests/aie/common/inc"
