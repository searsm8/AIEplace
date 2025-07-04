GCC := g++#${CXX} # For CPU only build, set to g++
TOP_DIR := ../../..
PROJ_NAME := AIEplace
MAIN = main.cpp
ITEMS := AIEplace DataBase Grid Net DCT GraphDriver Logger Common
ITEMS += Visualizer

OBJS := $(addprefix objs/, $(addsuffix .o, ${ITEMS}))
EXE := ${TOP_DIR}/vck5000/bin/AIEplace.exe

THIRD_PARTY := ${TOP_DIR}/third_party
LIMBO_DIR := ${THIRD_PARTY}/Limbo
TABLE_DIR := ${THIRD_PARTY}/tabulate/include
TBB_INCLUDE := ${HOME}/.local/include
TBB_LIB := ${HOME}/.local/lib64

INCLUDES := -I ./include
INCLUDES += -I${LIMBO_DIR} -I${TABLE_DIR} -I${TBB_INCLUDE}
INCLUDES += -I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include/ -I/lustre/home/nx08/shared/fpga/xilinx/include/ 
INCLUDES += -I${HOME}/local/boost_1_82_0/

COMPILE_FLAGS := -std=c++17
COMPILE_FLAGS += -g #see the proper names of variables and functions in your stack frames, 
				    #and get line numbers and see the source as you step around in the executable
COMPILE_FLAGS += -Llib 
COMPILE_FLAGS += -L${TBB_LIB}
COMPILE_FLAGS += -llefparseradapt 
COMPILE_FLAGS += -ldefparseradapt 
#COMPILE_FLAGS += -lverilogparser
COMPILE_FLAGS += -lbookshelfparser
COMPILE_FLAGS += -lgzstream -lz
COMPILE_FLAGS += -ltbb

# Cairo is used for generating images, but disabled now because it causes compile errors:
# warning: libnvidia-tls.so.430.50, needed by //usr/lib64/libGL.so.1, not found
COMPILE_FLAGS += -lcairo
#COMPILE_FLAGS += -l:libcairo.so.2# Specific library for use on nextgenio-amd02 node
COMPILE_FLAGS += -lstdc++fs
COMPILE_FLAGS += -lxrt_coreutil -lxilinxopencl
#COMPILE_FLAGS += -O0 # optimization level, 0 means no optimization
COMPILE_FLAGS += -O1 # optimization level, 3 means full optimization
COMPILE_FLAGS += -g -L$(XILINX_XRT)/lib/ -lpthread -lrt -lstdc++
#COMPILE_FLAGS += -Wall

COMPILE_FLAGS += -D_GLIBCXX_USE_CXX11_ABI=0
#COMPILE_FLAGS += -DCREATE_VISUALIZATION# Comment this out if no visualization is wanted.
#COMPILE_FLAGS += -DUSE_AIE_ACCELERATION# Comment this out if no visualization is wanted.

PROFILE_FLAG := -pg# flag for profiling with gprof
#PROFILE_FLAG += -fsanitize=thread# flag for thread sanitizer for debugging
#PROFILE_FLAG += -g# flag for code annotation


.PHONY: all objs

all: ${EXE}

${EXE}: ${MAIN} ${OBJS}
	@mkdir -p ${TOP_DIR}/bin
	@printf "\n\tMAKE INFO: Compile AIEplace executable \"$@\"\n"
	${GCC} ${PROFILE_FLAG} -o ${EXE} ${MAIN} ${OBJS} ${INCLUDES} ${COMPILE_FLAGS}

objs: ${OBJS}

objs/%.o: ./%.cpp ./include/%.h ./include/Common.h
	@mkdir -p objs
	@printf "\n\tMAKE INFO: Compile object file \"$@\"\n"
	${GCC} -c ${PROFILE_FLAG}  $< ${INCLUDES} ${COMPILE_FLAGS} -o $@

clean:
	rm -rf objs ${EXE} 
