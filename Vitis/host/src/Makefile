

CREATE_VISUALIZATION = true# Comment this if no visualization is needed.

GCC := g++#${CXX} # For CPU only build, set to g++
TOP_DIR ?= ..
MAIN = main.cpp
ITEMS := AIEplace DataBase Grid Net DCT 
ifdef CREATE_VISUALIZATION 
ITEMS += Visualizer
endif

OBJS := $(addprefix objs/, $(addsuffix .o, ${ITEMS}))
EXE := ${TOP_DIR}/AIEplace.exe

LIMBO_DIR := ~/AIEplace/third_party/Limbo

INCLUDES := -I ./include
INCLUDES += -I ${LIMBO_DIR}

COMPILE_FLAGS := -std=c++17
COMPILE_FLAGS += -L ${LIMBO_DIR}/lib 
COMPILE_FLAGS += -llefparseradapt 
COMPILE_FLAGS += -ldefparseradapt 
#COMPILE_FLAGS += -lverilogparser
#COMPILE_FLAGS += -lbookshelfparser
COMPILE_FLAGS += -lcairo
COMPILE_FLAGS += -lstdc++fs
COMPILE_FLAGS += -D_GLIBCXX_USE_CXX11_ABI=0

ifdef CREATE_VISUALIZATION 
COMPILE_FLAGS += -DCREATE_VISUALIZATION
endif

.PHONY: all exe objs

all: ${EXE}

${EXE}: ${MAIN} ${OBJS}
	${GCC} -o ${EXE} ${MAIN} ${OBJS} ${INCLUDES} ${COMPILE_FLAGS}

objs: ${OBJS}

objs/%.o: ./%.cpp ./include/%.h ./include/Common.h
	mkdir -p objs
	${GCC} -c $< ${INCLUDES} ${COMPILE_FLAGS} -o $@

clean:
	rm -rf objs ${EXE} 