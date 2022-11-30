#
#Copyright 2020 Xilinx, Inc.
#Licensed under the Apache License, Version 2.0 (the "License");
#you may not use this file except in compliance with the License.
#You may obtain a copy of the License at
#    http://www.apache.org/licenses/LICENSE-2.0
#Unless required by applicable law or agreed to in writing, software
#distributed under the License is distributed on an "AS IS" BASIS,
#WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#See the License for the specific language governing permissions and
#limitations under the License.
#
SDKTARGETSYSROOT ?= ${SYSROOT}

HOST_EXE     = ../host.exe
HOST_INC   = -I../ -I../aie
HOST_OBJ   = aie_control_xrt.o graph.o
AIE_CTRL_CPP = ../Work/ps/c_rts/aie_control_xrt.cpp
GRAPH_CPP = ../aie/graph.cpp
SDKTARGETSYSROOT ?= ${SYSROOT}

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
GCC_INCLUDES += -I../aie
GCC_INCLUDES += -I../aie/kernels
GCC_INCLUDES += -I$(SDKTARGETSYSROOT)/usr/include/xrt
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/include/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/src/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/tests/aie/inc
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L1/tests/aie/src
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L2/include/aie
GCC_INCLUDES += -I$(DSPLIB_ROOT)/L2/tests/aie/common/inc

${HOST_EXE}: ${HOST_OBJ}
	${CXX} $^ ${LDFLAGS} -std=c++14 -o $@ 

%.o: %.cpp
	${CXX} ${GCC_FLAGS} ${GCC_INCLUDES} -o $@ $<

aie_control_xrt.cpp: ${AIE_CTRL_CPP}
	cp -f ${AIE_CTRL_CPP} .
graph.cpp: ${GRAPH_CPP}
	cp -f ${GRAPH_CPP} .

clean:
	rm -rf ${HOST_OBJ} .Xil ${HOST_EXE} aie_control_xrt.cpp
