
.PHONY: all

GCC := g++ #${CXX} # For CPU only build, set to g++
TOP_DIR ?= ..
MAIN = main.cpp
ITEMS:= AIEplace DataBase Grid Net DCT #Visualizer
OBJS := $(addsuffix .o, ${ITEMS})
EXE := ${TOP_DIR}/AIEplace.exe

INCLUDES := -I ./include
INCLUDES += -I ${LIMBO_DIR}

COMPILE_FLAGS := -std=c++17
COMPILE_FLAGS += -L ${LIMBO_DIR}/lib 
COMPILE_FLAGS += -llefparseradapt 
COMPILE_FLAGS += -ldefparseradapt 
#COMPILE_FLAGS += -lverilogparser
#COMPILE_FLAGS += -lbookshelfparser
COMPILE_FLAGS += -lcairo
COMPILE_FLAGS += -D_GLIBCXX_USE_CXX11_ABI=0

all: ${EXE}

${EXE}: ${MAIN} ${OBJS}
	${GCC} -o ${EXE} ${MAIN} ${OBJS} ${INCLUDES} ${COMPILE_FLAGS}

objs: ${OBJS}

%.o: %.cpp ./include/%.h ./include/Common.h
	${GCC} -c $< ${INCLUDES} ${COMPILE_FLAGS} -o $@

clean:
	rm -rf ${EXE} *.o