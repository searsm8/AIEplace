
.PHONY: all

TOP := AIEplace.cpp
TOP_DIR ?= ..
ITEMS:= AIEplace Database Net Node Position MacroClass
OBJS := $(addsuffix .o, ${ITEMS})
EXE := ${TOP_DIR}/AIEplace.exe

INCLUDES := -I ../include
INCLUDES += -I ${LIMBO_DIR}

COMPILE_FLAGS := -L ${LIMBO_DIR}/lib 
COMPILE_FLAGS += -llefparseradapt 
COMPILE_FLAGS += -ldefparseradapt 
#COMPILE_FLAGS += -lverilogparser
#COMPILE_FLAGS += -lbookshelfparser
COMPILE_FLAGS += -D_GLIBCXX_USE_CXX11_ABI=0

all: ${OBJS}
	g++ -o ${EXE} ${OBJS} ${COMPILE_FLAGS}

%.o: %.cpp ../include/%.h
	g++ -c $< ${INCLUDES} ${COMPILE_FLAGS} -o $@

clean:
	rm -rf ${EXE} *.o