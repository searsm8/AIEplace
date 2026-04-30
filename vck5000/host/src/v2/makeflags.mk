
BUILD_XRT ?= 0# Set to 1 to build with XRT and VCK5000 support.
BUILD_VIZ ?= 0# Set to 1 to build with visualization support.

THIRD_PARTY = $(PROJECT_ROOT)/../third_party

HOST_MAIN = main.cpp
HOST_SRCS = Library.cpp Parsers.cpp Common.cpp Logger.cpp Net.cpp
#HOST_SRCS = AIEplace.cpp Partials.cpp \
#	    Density.cpp Output.cpp DataBase.cpp \
#	    Grid.cpp Net.cpp DCT.cpp Logger.cpp \
#	    Common.cpp JsonUtils.cpp
HOST_OBJS = $(addprefix $(BUILD_DIR_HOST)/obj/, $(HOST_MAIN:.cpp=.o) $(HOST_SRCS:.cpp=.o))
HOST_DEPS = $(HOST_OBJS:.o=.d)

# General
CPPFLAGS += -I $(HOST_DIR)/include
CPPFLAGS += -I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include/

CXXFLAGS += -std=c++2a
CXXFLAGS += -g
CXXFLAGS += -O0 # optimization level, 0 means no optimization
#CXXFLAGS += -O1 # optimization level, 3 means full optimization
#CXXFLAGS += -Wall

LDFLAGS += -L$(HOST_DIR)/lib
LDLIBS += -lpthread -lrt -lstdc++
LDLIBS += -lstdc++fs

# Profiling
#CXXFLAGS += -pg# flag for profiling with gprof
#CXXFLAGS += -fsanitize=thread# flag for thread sanitizer for debugging

# Parsers
LDLIBS += -llefparseradapt
LDLIBS += -ldefparseradapt
#LDLIBS += -lverilogparser
LDLIBS += -lbookshelfparser -lgzstream -lz

#Boost
CPPFLAGS += -I${HOME}/local/boost_1_82_0/

# Limbo
LIMBO_DIR = ${THIRD_PARTY}/Limbo
CPPFLAGS += -I${LIMBO_DIR} -I${TABLE_DIR}/include
CPPFLAGS += -D_GLIBCXX_USE_CXX11_ABI=0

# Tabulate
TABLE_DIR = ${THIRD_PARTY}/tabulate

# TDD (Disabled for WSL)
TBB_INCLUDE = ${HOME}/.local/include
TBB_LIB = ${HOME}/.local/lib64
#CPPFLAGS += -I${TBB_INCLUDE}
#LDFLAGS += -L${TBB_LIB}
#LDLIBS += -ltbb  # Disabled for WSL build - enable with -DUSE_TBB if needed


# Enable XRT acceleration
ifeq (BUILD_XRT, 1)
HOST_SRCS += GraphDriver.cpp
CXXFLAGS += -DUSE_AIE_ACCELERATION
CPPFLAGS += -DUSE_XILINX_XRT=1
LDFLAGS += -L$(XILINX_XRT)/lib/
LDLIBS += -lxrt_coreutil -lxrt++ -lxilinxopencl
endif

# Enable Vizualization
ifeq (BUILD_VIZ, 1)
HOST_SRCS += Visualizer.cpp
CPPFLAGS += -DCREATE_VISUALIZATION=1
LDLIBS += -lcairo
# Cairo is used for generating images, but disabled now because it causes compile errors:
# warning: libnvidia-tls.so.430.50, needed by //usr/lib64/libGL.so.1, not found
#LDLIBS += -l:libcairo.so.2# Specific library for use on nextgenio-amd02 node
endif

