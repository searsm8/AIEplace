# Parser timing / equivalence harness (TODO #43). build/parse_bench is the native DesignReader
# with its own copy of host/src/common, built exactly like the host at sw_only's optimisation level.
#   make            build it
#   make PROFILE=1  gprof builds under build_prof/ (this box has no perf)
#   make clean
REPO      = ../../..
COMMON    = $(REPO)/host/src/common
THIRD     = $(REPO)/third_party
BUILD     = build

CXX       = g++
CXXFLAGS  = -std=c++2a -O2 -g -fopenmp -MMD -MP
CPPFLAGS  = -I$(COMMON)/include -I$(THIRD)/tabulate/include
LDFLAGS   = -fopenmp -no-pie
LDLIBS    = -lstdc++fs -ldl

ifdef PROFILE
BUILD     = build_prof
CXXFLAGS += -pg
LDFLAGS  += -pg
endif

SRCS        = parse_bench.cpp DataBase.cpp DesignReader.cpp Grid.cpp Net.cpp Logger.cpp Common.cpp
NATIVE_OBJS = $(addprefix $(BUILD)/native/, $(SRCS:.cpp=.o))
vpath %.cpp . $(COMMON)/src

.PHONY: all clean
all: $(BUILD)/parse_bench

$(BUILD)/parse_bench: $(NATIVE_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/native/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

-include $(wildcard $(BUILD)/*/*.d)

clean:
	rm -rf build build_prof
