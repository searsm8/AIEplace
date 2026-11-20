# Compiler
CXX      := g++
# Xilinx XRT paths (expected to be set in environment)
XRT_INC  := $(XILINX_XRT)/include
XRT_LIB  := $(XILINX_XRT)/lib

# Target
TARGET   := dct_realonly
#SRC      := main_parallel_dct.cpp
SRC      := main_density_accelerator.cpp

# Flags
CXXFLAGS := -std=c++17 -I$(XRT_INC)
LDFLAGS  := -L$(XRT_LIB)
LDLIBS   := -lxrt_coreutil -lxrt++ -lxilinxopencl

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f $(TARGET)
