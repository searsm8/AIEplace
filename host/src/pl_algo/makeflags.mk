# pl_algo host: lean IO/policy driver around the PL.
# Shares the parser (DataBase + Limbo) and data model with sw_only via host/src/common;
# the CPU solver and visualizer are intentionally absent (the algorithm runs on the PL).

THIRD_PARTY = $(PROJECT_ROOT)/../third_party

HOST_MAIN = main.cpp
HOST_SRCS = Packer.cpp
# Parser + data model, shared with sw_only -- see host/src/common (TODO #9).
COMMON_SRCS = DataBase.cpp Net.cpp Common.cpp Logger.cpp Grid.cpp

HOST_OBJS = $(addprefix $(BUILD_DIR_HOST)/obj/, $(HOST_MAIN:.cpp=.o) $(HOST_SRCS:.cpp=.o) $(COMMON_SRCS:.cpp=.o))
HOST_DEPS = $(HOST_OBJS:.o=.d)

# Includes. pl_algo has no include/ of its own -- its headers sit next to their .cpp in src/.
CPPFLAGS += -I $(HOST_COMMON_DIR)/include
CPPFLAGS += -I $(PROJECT_ROOT)/pl/src/pl_algo/src  # host_interface.hpp (shared host<->PL contract)

CXXFLAGS += -std=c++2a -g -O0

LDLIBS += -lpthread -lrt -lstdc++ -lstdc++fs

# Parsers (Limbo) -- a git SUBMODULE (third_party/Limbo, upstream tag 3.5.2). Headers from the
# submodule source tree, libs from the out-of-tree build the bootstrap script produces; nothing
# Limbo-related is checked in. Fresh clone: run tools/bootstrap_third_party.sh first.
LIMBO_DIR     = ${THIRD_PARTY}/Limbo
LIMBO_LIB_DIR = ${THIRD_PARTY}/limbo_install/lib
LDFLAGS += -L${LIMBO_LIB_DIR}
LDLIBS  += -llefparseradapt -ldefparseradapt -lbookshelfparser -lgzstream -lz
CPPFLAGS += -I${LIMBO_DIR}   # Boost: header-only, no -I on purpose -- see sw_only/makeflags.mk
CPPFLAGS += -D_GLIBCXX_USE_CXX11_ABI=0
CPPFLAGS += -I${THIRD_PARTY}/tabulate/include   # header-only, used by Logger

# XRT (PL kernel driver).
ifdef BUILD_XRT
HOST_SRCS += Driver.cpp HpwlGradVerify.cpp DensityVerify.cpp DCT1DVerify.cpp TransposeVerify.cpp FieldVerify.cpp ForceVerify.cpp IterVerify.cpp MetricsVerify.cpp
CPPFLAGS += -DUSE_XILINX_XRT -I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include/
LDFLAGS  += -L$(XILINX_XRT)/lib/
LDLIBS   += -lxrt_coreutil
# libxrt is built with the new GLIBCXX ABI, but the rest of the host uses the old
# ABI for Limbo. Compile ONLY the XRT TU with the new ABI; it shares no
# std::string with the parser side (PackedDesign is std::vector-only, the xclbin
# path is const char*), so the two ABIs coexist safely in one binary.
$(BUILD_DIR_HOST)/obj/Driver.o: CPPFLAGS += -U_GLIBCXX_USE_CXX11_ABI -D_GLIBCXX_USE_CXX11_ABI=1
endif
