# sw_flat (#45): the one parse and the software placer. Targets grow with the plan's steps. Meow.
#
#   make check-database   FlatDatabase vs sw_only's DataBase and the legacy packer reader on all 44
#                         designs (test/flat_database_check.sh); exits non-zero on any difference. ~2 min.
#   make check-layout     the whole-design slot layout (slot_layout.hpp) and fillers on all 44 designs
#                         (test/slot_layout_check.sh): invariants, fillers bit-equal to DataBase's. ~3 min.
#   make netlists        the packer's Netlist file for each of the 44 designs, in build/netlists/
#                         (<suite>_<name>.netlist), for pl/test and model/. Use -j4. Rebuilt when
#                         export_netlist (so the parser or flat_netlist.hpp) changes.
#   make clean

REPO     := ../..
COMMON   := $(REPO)/host/src/common
BUILD    := build

CXX      ?= g++
CXXFLAGS  = -std=c++2a -O2 -g -fopenmp -MMD -MP
CPPFLAGS  = -I. -I$(REPO)/pl/src -I$(COMMON)/include -I$(REPO)/third_party/tabulate/include
LDFLAGS   = -fopenmp -no-pie
LDLIBS    = -lstdc++fs -ldl

COMMON_SRCS = DataBase.cpp DesignReader.cpp Grid.cpp Net.cpp Logger.cpp Common.cpp
COMMON_OBJS = $(addprefix $(BUILD)/, $(COMMON_SRCS:.cpp=.o))
vpath %.cpp . test $(COMMON)/src

# The 44 designs, as "<suite>/<name>"; a netlist file is named <suite>_<name> (no suite has a '_'). Meow.
DESIGNS      = $(shell cd $(REPO)/vck5000/tools && python3 -c 'import benchmarks; print(" ".join(sorted(benchmarks.BENCHMARKS)))')
NETLIST_DIR  = $(BUILD)/netlists
NETLISTS     = $(foreach d,$(DESIGNS),$(NETLIST_DIR)/$(subst /,_,$(d)).netlist)
suite_of     = $(firstword $(subst _, ,$(1)))

.PHONY: check-database check-layout netlists clean
check-database: $(BUILD)/flat_database_check
	bash test/flat_database_check.sh

check-layout: $(BUILD)/slot_layout_check
	bash test/slot_layout_check.sh

netlists: $(NETLISTS)

$(NETLIST_DIR)/%.netlist: $(BUILD)/export_netlist
	@mkdir -p $(NETLIST_DIR)
	$(BUILD)/export_netlist $(REPO)/host/benchmarks/$(call suite_of,$*)/$(patsubst $(call suite_of,$*)_%,%,$*) $@

$(BUILD)/flat_database_check: $(BUILD)/flat_database.o $(BUILD)/flat_database_check.o $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/slot_layout_check: $(BUILD)/flat_database.o $(BUILD)/slot_layout_check.o $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/export_netlist: $(BUILD)/flat_database.o $(BUILD)/export_netlist.o $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

-include $(wildcard $(BUILD)/*.d)

clean:
	rm -rf $(BUILD)
