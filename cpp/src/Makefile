
.PHONY: all

TOP := aieplace.cpp
ITEMS:= main database
OBJS := $(addsuffix .o, ${ITEMS})
EXE := ${TOP_DIR}/aieplace.exe

INCLUDES := -I ../include
INCLUDES += -I ../Limbo/include 

COMPILE_FLAGS := -L ${LIMBO_DIR}/lib 
COMPILE_FLAGS += -llefparseradapt 
COMPILE_FLAGS += -ldefparseradapt 
COMPILE_FLAGS += -lverilogparser
COMPILE_FLAGS += -lbookshelfparser
COMPILE_FLAGS += -D_GLIBCXX_USE_CXX11_ABI=0

all: ${OBJS}
	g++ -o ${EXE} ${OBJS} ${COMPILE_FLAGS}

%.o: %.cpp
	g++ -c $< ${INCLUDES} ${COMPILE_FLAGS} -o $@

clean:
	rm -rf ${EXE} *.o