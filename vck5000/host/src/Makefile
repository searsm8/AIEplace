GCC := g++#${CXX} # For CPU only build, set to g++
TOP_DIR := ../../..
PROJ_NAME := AIEplace
MAIN = main.cpp

BUILD_XRT ?= # Set to 1 to build with XRT and VCK5000 support.
BUILD_VIZ ?= 1# Set to 1 to build with visualization support.

ITEMS := AIEplace Partials Density Output DataBase Grid Net DCT Logger Common JsonUtils
# Auto-detect XRT environment (set on HPC with VCK5000 access)
ifdef BUILD_XRT
$(info MAKE INFO: Building with XRT support for VCK5000)
ITEMS += GraphDriver
else
$(info MAKE INFO: Building without XRT - CPU only mode)
endif

ifdef BUILD_VIZ
ITEMS += Visualizer
endif

OBJS := $(addprefix objs/, $(addsuffix .o, ${ITEMS}))
EXE := ${TOP_DIR}/vck5000/bin/AIEplace.exe

THIRD_PARTY := ${TOP_DIR}/third_party
LIMBO_DIR := ${THIRD_PARTY}/Limbo
TABLE_DIR := ${THIRD_PARTY}/tabulate/include
TBB_INCLUDE := ${HOME}/.local/include
TBB_LIB := ${HOME}/.local/lib64

INCLUDES := -I ./include
INCLUDES += -I${LIMBO_DIR} -I${TABLE_DIR} #-I${TBB_INCLUDE}  # TBB disabled for WSL
INCLUDES += -I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include/ -I/lustre/home/nx08/shared/fpga/xilinx/include/ 
INCLUDES += -I${HOME}/local/boost_1_82_0/

COMPILE_FLAGS := -std=c++2a
COMPILE_FLAGS += -g # Debugging flag: see the proper names of variables and functions in your stack frames,
				    # and get line numbers and see the source as you step around in the executable
ifdef BUILD_XRT
COMPILE_FLAGS += -DUSE_AIE_ACCELERATION
endif

LINK_FLAGS := -Llib

# Conditional XRT linking
ifdef BUILD_XRT
    COMPILE_FLAGS += -DUSE_XILINX_XRT
    #LINK_FLAGS += -L${TBB_LIB}
    LINK_FLAGS += -L$(XILINX_XRT)/lib/
    LINK_FLAGS += -lxrt_coreutil -lxrt++ -lxilinxopencl
endif

LINK_FLAGS += -llefparseradapt
LINK_FLAGS += -ldefparseradapt
#LINK_FLAGS += -lverilogparser
LINK_FLAGS += -lbookshelfparser
LINK_FLAGS += -lgzstream -lz
LINK_FLAGS += -lstdc++fs
LINK_FLAGS += -lpthread -lrt -lstdc++

#LINK_FLAGS += -ltbb  # Disabled for WSL build - enable with -DUSE_TBB if needed
# Cairo is used for generating images, but disabled now because it causes compile errors:
# warning: libnvidia-tls.so.430.50, needed by //usr/lib64/libGL.so.1, not found
ifdef BUILD_VIZ
$(info MAKE INFO: Building with visualization support)
LINK_FLAGS += -lcairo
COMPILE_FLAGS += -DCREATE_VISUALIZATION
endif
#LINK_FLAGS += -l:libcairo.so.2# Specific library for use on nextgenio-amd02 node


COMPILE_FLAGS += -O0 # optimization level, 0 means no optimization
#COMPILE_FLAGS += -O1 # optimization level, 3 means full optimization
#COMPILE_FLAGS += -Wall

COMPILE_FLAGS += -D_GLIBCXX_USE_CXX11_ABI=0  # Required for Limbo library compatibility

PROFILE_FLAG :=  # Disabled profiling for WSL debug
#PROFILE_FLAG := -pg# flag for profiling with gprof
#PROFILE_FLAG += -fsanitize=thread# flag for thread sanitizer for debugging
#PROFILE_FLAG += -g# flag for code annotation

.PHONY: all objs

all: ${EXE}

${EXE}: ${MAIN} ${OBJS}
	@mkdir -p ${TOP_DIR}/bin
	$(info MAKE INFO: Compile AIEplace executable.)
	${GCC} ${PROFILE_FLAG} -o ${EXE} ${MAIN} ${OBJS} ${INCLUDES} ${COMPILE_FLAGS} ${LINK_FLAGS}

objs: ${OBJS}

objs/%.o: ./%.cpp ./include/Common.h
	@mkdir -p objs
	$(info MAKE INFO: Compile object file "$@")
	${GCC} -c ${PROFILE_FLAG}  $< ${INCLUDES} ${COMPILE_FLAGS} -o $@

clean:
	rm -rf objs ${EXE} 
