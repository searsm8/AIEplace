# pl_algo host: lean IO/policy driver around the PL.
# Shares the parser (DataBase + DesignReader) and data model with sw_only via host/src/common;
# the CPU solver and visualizer are intentionally absent (the algorithm runs on the PL).

THIRD_PARTY = $(PROJECT_ROOT)/../third_party

HOST_MAIN = main.cpp
HOST_SRCS = Packer.cpp
# Parser + data model, shared with sw_only -- see host/src/common (TODO #9).
COMMON_SRCS = DataBase.cpp DesignReader.cpp Net.cpp Common.cpp Logger.cpp Grid.cpp

HOST_OBJS = $(addprefix $(BUILD_DIR_HOST)/obj/, $(HOST_MAIN:.cpp=.o) $(HOST_SRCS:.cpp=.o) $(COMMON_SRCS:.cpp=.o))
HOST_DEPS = $(HOST_OBJS:.o=.d)

# Includes. pl_algo has no include/ of its own -- its headers sit next to their .cpp in src/.
CPPFLAGS += -I $(HOST_COMMON_DIR)/include
CPPFLAGS += -I $(PROJECT_ROOT)/pl/src/pl_algo/src  # host_interface.hpp (shared host<->PL contract)

CXXFLAGS += -std=c++2a -g -O0

LDLIBS += -lpthread -lrt -lstdc++ -lstdc++fs

# Parsers: native, host/src/common/src/DesignReader.cpp (TODO #43) -- nothing to link.
CPPFLAGS += -I${THIRD_PARTY}/tabulate/include   # header-only, used by Logger

# XRT (PL kernel driver).
ifdef BUILD_XRT
HOST_SRCS += Driver.cpp HpwlGradVerify.cpp DensityVerify.cpp DCT1DVerify.cpp TransposeVerify.cpp FieldVerify.cpp ForceVerify.cpp IterVerify.cpp MetricsVerify.cpp
CPPFLAGS += -DUSE_XILINX_XRT -I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include/
LDFLAGS  += -L$(XILINX_XRT)/lib/
LDLIBS   += -lxrt_coreutil
endif
