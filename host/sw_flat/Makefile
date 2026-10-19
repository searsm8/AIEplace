# sw_flat (#45): the one parse and the software placer. Targets grow with the plan's steps. Meow.
#
#   make check-database   FlatDatabase vs sw_only's DataBase and the legacy packer reader on all 44
#                         designs (test/flat_database_check.sh); exits non-zero on any difference. ~2 min.
#   make check-layout     the whole-design slot layout (slot_layout.hpp) and fillers on all 44 designs
#                         (test/slot_layout_check.sh): invariants, fillers bit-equal to DataBase's. ~3 min.
#   make netlists        the packer's Netlist file for each of the 44 designs, in build/netlists/
#                         (<suite>_<name>.netlist), for pl/test and model/. Use -j4. Rebuilt when
#                         export_netlist (so the parser or flat_netlist.hpp) changes.
#   make placer          build/sw_flat_place, the CPU phase-1 placer (placer/, step 5).
#   make check-placer     sw_flat_place vs sw_only on the D6 configs (test/placer_check.sh): the
#                         schedule traces must be identical text. Builds sw_only first. ~1 min.
#   make check-density-map  density_map_CU (pl/src/modules) vs sw_flat's own deposit on the 44 designs at
#                         grid <= 1024, 30 iterations in, B' on: rho bit for bit, the overflow to a stated
#                         tolerance (test/density_map_check.sh). Use JOBS=n.
#   make check-overflow-map  #48 option B': the overflow reduced from the solver's map before the fillers,
#                         over an exact fixed base, vs sw_only's separate metric, 44 designs
#                         (test/overflow_map_check.sh). Use JOBS=n, ITERATIONS=n.
#   make gradient-bench   build/gradient_bench: the WA gradient with std::exp vs the PL's table, timed.
#   make check-record-stream  sw_flat's record-stream gradient and HPWL (placer/record_stream.cpp) vs
#                         hpwl_gradient_CU on the same positions, bit for bit, 6 designs
#                         (test/record_stream_check.sh).
#   make hpwl-gradient-xrt  hpwl_gradient_CU through XRT (pl/'s `make xclbin`) vs the same CU in g++, bit for
#                         bit (test/hpwl_gradient_xrt.sh). Needs `source ../../setup_env.sh`; TARGET=sw_emu
#                         (default) | hw_emu | hw picks the xclbin and the emulation mode.
#   make clean

REPO     := ../..
COMMON   := $(REPO)/host/src/common
BUILD    := build

CXX      ?= g++
CXXFLAGS  = -std=c++2a -O2 -g -fopenmp -MMD -MP
CPPFLAGS  = -I. -I$(REPO)/pl/src -I$(COMMON)/include -I$(REPO)/third_party/tabulate/include \
            -I$(REPO)/host/src/sw_only/include
LDFLAGS   = -fopenmp -no-pie
LDLIBS    = -lstdc++fs -ldl

COMMON_SRCS = DataBase.cpp DesignReader.cpp Grid.cpp Net.cpp Logger.cpp Common.cpp
COMMON_OBJS = $(addprefix $(BUILD)/, $(COMMON_SRCS:.cpp=.o))
vpath %.cpp . test $(COMMON)/src placer $(REPO)/host/src/sw_only/src

# DCT.cpp is sw_only's own transform, linked rather than copied: the field must round the same. Meow.
PLACER_SRCS = placer.cpp setup.cpp wirelength.cpp record_stream.cpp density.cpp step.cpp schedule.cpp main.cpp DCT.cpp
PLACER_OBJS = $(addprefix $(BUILD)/, $(PLACER_SRCS:.cpp=.o))
PLACER_LIB  = $(filter-out $(BUILD)/main.o, $(PLACER_OBJS))

# The 44 designs, as "<suite>/<name>"; a netlist file is named <suite>_<name> (no suite has a '_'). Meow.
DESIGNS      = $(shell cd $(REPO)/vck5000/tools && python3 -c 'import benchmarks; print(" ".join(sorted(benchmarks.BENCHMARKS)))')
NETLIST_DIR  = $(BUILD)/netlists
NETLISTS     = $(foreach d,$(DESIGNS),$(NETLIST_DIR)/$(subst /,_,$(d)).netlist)
suite_of     = $(firstword $(subst _, ,$(1)))

.PHONY: check-database check-layout netlists placer check-placer check-density-map check-overflow-map gradient-bench check-record-stream hpwl-gradient-xrt clean
check-database: $(BUILD)/flat_database_check
	bash test/flat_database_check.sh

check-layout: $(BUILD)/slot_layout_check
	bash test/slot_layout_check.sh

netlists: $(NETLISTS)

placer: $(BUILD)/sw_flat_place

gradient-bench: $(BUILD)/gradient_bench

check-density-map: $(BUILD)/density_map_check
	bash test/density_map_check.sh

$(BUILD)/density_map_check: $(BUILD)/flat_database.o $(BUILD)/density_map_check.o $(PLACER_LIB) $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

check-overflow-map: $(BUILD)/overflow_map_check
	bash test/overflow_map_check.sh

$(BUILD)/overflow_map_check: $(BUILD)/flat_database.o $(BUILD)/overflow_map_check.o $(PLACER_LIB) $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/gradient_bench: $(BUILD)/flat_database.o $(BUILD)/gradient_bench.o $(PLACER_LIB) $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

check-record-stream: $(BUILD)/record_stream_check
	bash test/record_stream_check.sh

$(BUILD)/record_stream_check: $(BUILD)/flat_database.o $(BUILD)/record_stream_check.o $(PLACER_LIB) $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

TARGET     ?= sw_emu
XILINX_XRT ?= /opt/xilinx/xrt
hpwl-gradient-xrt: $(BUILD)/hpwl_gradient_xrt
	TARGET=$(TARGET) bash test/hpwl_gradient_xrt.sh

$(BUILD)/hpwl_gradient_xrt.o: CPPFLAGS += -I$(XILINX_XRT)/include -Ipacker
$(BUILD)/hpwl_gradient_xrt: $(BUILD)/flat_database.o $(BUILD)/hpwl_gradient_xrt.o $(PLACER_LIB) $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS) -L$(XILINX_XRT)/lib -lxrt_coreutil -luuid -pthread

check-placer: $(BUILD)/sw_flat_place
	bash test/placer_check.sh

$(BUILD)/sw_flat_place: $(BUILD)/flat_database.o $(PLACER_OBJS) $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

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
