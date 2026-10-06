# beat_packer -- host prototype for the conflict-free scatter-add packing (#41). Plain g++, no Vitis.
#   make            build beat_packer and chunk_profile
#   make run        every manifest design (ISPD2005 + ISPD2015 + MMS), banks=32 hazard=4
#   make run ARGS="--banks 32 --hazard 6"
#   make check-reader   the host reader (native_netlist.hpp) vs the legacy one, every design
#   make partition_study large_net_stats
BENCH ?= /home/msears/phd/AIEplace/host/benchmarks
ARGS  ?=

ISPD2005 = adaptec1 adaptec2 adaptec3 adaptec4 bigblue1 bigblue2 bigblue3 bigblue4
ISPD2015 = $(notdir $(wildcard $(BENCH)/ispd2015/mgc_*))
MMS      = adaptec1 adaptec2 adaptec3 adaptec4 adaptec5 bigblue1 bigblue2 bigblue3 bigblue4 \
           newblue1 newblue2 newblue3 newblue4 newblue5 newblue6 newblue7

DESIGNS = $(foreach d,$(ISPD2005),--bookshelf $(BENCH)/ispd2005/$(d) $(d)) \
          $(foreach d,$(ISPD2015),--def $(BENCH)/ispd2015/$(d)/floorplan.def $(d)) \
          $(foreach d,$(MMS),--bookshelf $(BENCH)/mms/$(d) $(d))

# The tools read designs with the host's own reader (native_netlist.hpp, #43), so they link its
# objects. -fopenmp is the reader's: it reads a file's chunks in parallel. beat_packer.hpp itself
# stays header-only for the tier-1 harnesses and HLS testbenches. Meow.
COMMON     = ../../../host/src/common
COMMON_SRC = DesignReader.cpp Logger.cpp Grid.cpp Net.cpp Common.cpp
COMMON_OBJ = $(addprefix build/, $(COMMON_SRC:.cpp=.o))
CXXFLAGS   = -std=c++2a -O2 -fopenmp -I. -I$(COMMON)/include -I../../../third_party/tabulate/include
LDLIBS     = -lstdc++fs
HEADERS    = beat_packer.hpp native_netlist.hpp pin_record.hpp $(wildcard $(COMMON)/include/*.h)

all: beat_packer chunk_profile

build/%.o: $(COMMON)/src/%.cpp $(wildcard $(COMMON)/include/*.h)
	@mkdir -p build
	g++ $(CXXFLAGS) -c $< -o $@

beat_packer: beat_packer.cpp $(HEADERS) $(COMMON_OBJ)
	g++ $(CXXFLAGS) -Wall -o $@ $< $(COMMON_OBJ) $(LDLIBS)

run: beat_packer
	./beat_packer $(ARGS) $(DESIGNS)

check-reader: beat_packer
	./beat_packer --check-reader $(DESIGNS)

# #42: host start-up per stage + per-phase device cycle model, chunked at the real capacity.
chunk_profile: chunk_profile.cpp $(HEADERS) cycle_model.hpp ../mailbox_widened/mailbox_layout.hpp $(COMMON_OBJ)
	g++ $(CXXFLAGS) -Wall -I../mailbox_widened -o $@ $< $(COMMON_OBJ) $(LDLIBS)

profile: chunk_profile
	./chunk_profile $(ARGS) $(DESIGNS)

partition_study: partition_study.cpp partition.hpp cycle_model.hpp $(HEADERS) $(COMMON_OBJ)
	g++ $(CXXFLAGS) -Wall -I../mailbox_widened -o $@ $< $(COMMON_OBJ) $(LDLIBS)

large_net_stats: large_net_stats.cpp $(HEADERS) $(COMMON_OBJ)
	g++ $(CXXFLAGS) -Wall -o $@ $< $(COMMON_OBJ) $(LDLIBS)

clean:
	rm -rf beat_packer chunk_profile partition_study large_net_stats build

.PHONY: all run check-reader profile clean
