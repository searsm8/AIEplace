# Parser timing / equivalence harness (TODO #43). Two binaries, each with its own copy of
# host/src/common compiled at sw_only's optimisation level:
#   build/parse_bench        the native DesignReader, built exactly like the host
#   build/parse_bench_limbo  adds --limbo (the Limbo parsers through a bridge). Limbo is built with
#                            the old string ABI, so this binary and its objects must be too.
#   make            build both (the Limbo one needs tools/bootstrap_third_party.sh to have run)
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

LIMBO_FLAGS  = -DWITH_LIMBO -D_GLIBCXX_USE_CXX11_ABI=0 -I$(THIRD)/Limbo
LIMBO_LDLIBS = -L$(THIRD)/limbo_install/lib -llefparseradapt -ldefparseradapt -lbookshelfparser -lgzstream -lz

ifdef PROFILE
BUILD     = build_prof
CXXFLAGS += -pg
LDFLAGS  += -pg
endif

SRCS        = parse_bench.cpp DataBase.cpp DesignReader.cpp Grid.cpp Net.cpp Logger.cpp Common.cpp
NATIVE_OBJS = $(addprefix $(BUILD)/native/, $(SRCS:.cpp=.o))
LIMBO_OBJS  = $(addprefix $(BUILD)/limbo/, $(SRCS:.cpp=.o))
vpath %.cpp . $(COMMON)/src

.PHONY: all clean
all: $(BUILD)/parse_bench $(BUILD)/parse_bench_limbo

$(BUILD)/parse_bench: $(NATIVE_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/parse_bench_limbo: $(LIMBO_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS) $(LIMBO_LDLIBS)

$(BUILD)/native/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/limbo/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LIMBO_FLAGS) -c $< -o $@

-include $(wildcard $(BUILD)/*/*.d)

clean:
	rm -rf build build_prof
