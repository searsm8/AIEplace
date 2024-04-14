GCC := g++#${CXX} # For CPU only build, set to g++
TOP_DIR := ../..
MAIN = main.cpp
ITEMS := AIEplace DataBase Grid Net DCT GraphDriver
ITEMS += Visualizer

OBJS := $(addprefix objs/, $(addsuffix .o, ${ITEMS}))
EXE := ${TOP_DIR}/bin/AIEplace.exe

LIMBO_DIR := ${HOME}/AIEplace/third_party/Limbo

INCLUDES := -I ./include
INCLUDES += -I ${LIMBO_DIR}
INCLUDES += -I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include/ -I/lustre/home/nx08/shared/fpga/xilinx/include/ 

COMPILE_FLAGS := -std=c++17
COMPILE_FLAGS += -L${LIMBO_DIR}/lib 
COMPILE_FLAGS += -llefparseradapt 
COMPILE_FLAGS += -ldefparseradapt 
#COMPILE_FLAGS += -lverilogparser
#COMPILE_FLAGS += -lbookshelfparser
COMPILE_FLAGS += -lcairo
COMPILE_FLAGS += -lstdc++fs
COMPILE_FLAGS += -lxrt_coreutil -lxilinxopencl
COMPILE_FLAGS += -O0 -g -L$(XILINX_XRT)/lib/ -lpthread -lrt -lstdc++
#COMPILE_FLAGS += -Wall

COMPILE_FLAGS += -D_GLIBCXX_USE_CXX11_ABI=0
#COMPILE_FLAGS += -DCREATE_VISUALIZATION# Comment this out if no visualization is wanted.
#COMPILE_FLAGS += -DUSE_AIE_ACCELERATION# Comment this out if no visualization is wanted.

.PHONY: all exe objs

all: ${EXE}

${EXE}: ${MAIN} ${OBJS}
	${GCC} -o ${EXE} ${MAIN} ${OBJS} ${INCLUDES} ${COMPILE_FLAGS}

objs: ${OBJS}

objs/%.o: ./%.cpp ./include/%.h ./include/Common.h
	@mkdir -p objs
	${GCC} -c $< ${INCLUDES} ${COMPILE_FLAGS} -o $@

clean:
	rm -rf objs ${EXE} 