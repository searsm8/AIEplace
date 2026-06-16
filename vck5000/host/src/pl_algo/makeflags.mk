# pl_algo host: lean IO/policy driver around the PL.
# Reuses the markv1 parser (DataBase + Limbo) and data model; the CPU solver and
# visualizer are intentionally absent (the algorithm runs on the PL).

THIRD_PARTY = $(PROJECT_ROOT)/../third_party
MARKV1_DIR  = $(PROJECT_ROOT)/host/src/markv1

HOST_MAIN = main.cpp
# Frozen parser subset (copied from markv1) + the new packer.
HOST_SRCS = DataBase.cpp Net.cpp Common.cpp Logger.cpp Grid.cpp \
	    Packer.cpp

HOST_OBJS = $(addprefix $(BUILD_DIR_HOST)/obj/, $(HOST_MAIN:.cpp=.o) $(HOST_SRCS:.cpp=.o))
HOST_DEPS = $(HOST_OBJS:.o=.d)

# Includes
CPPFLAGS += -I $(HOST_DIR)/include
CPPFLAGS += -I $(PROJECT_ROOT)/pl/src/pl_algo/src  # host_interface.hpp (shared host<->PL contract)

CXXFLAGS += -std=c++2a -g -O0

LDLIBS += -lpthread -lrt -lstdc++ -lstdc++fs

# Parsers (Limbo) -- reuse markv1's prebuilt static libs.
LDFLAGS += -L$(MARKV1_DIR)/lib
LDLIBS  += -llefparseradapt -ldefparseradapt -lbookshelfparser -lgzstream -lz
CPPFLAGS += -I${THIRD_PARTY}/Limbo   # boost is found via the system include path
CPPFLAGS += -D_GLIBCXX_USE_CXX11_ABI=0
CPPFLAGS += -I${THIRD_PARTY}/tabulate/include   # header-only, used by Logger

# XRT (PL kernel driver) -- enabled once Driver.cpp lands (v0 step C).
ifdef BUILD_XRT
HOST_SRCS += Driver.cpp
CPPFLAGS += -DUSE_XILINX_XRT -I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include/
LDFLAGS  += -L$(XILINX_XRT)/lib/
LDLIBS   += -lxrt_coreutil
endif
