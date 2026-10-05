# beat_packer -- host prototype for the conflict-free scatter-add packing (#41). Plain g++, no Vitis.
#   make            build
#   make run        every manifest design (ISPD2005 + ISPD2015 + MMS), banks=32 hazard=4
#   make run ARGS="--banks 32 --hazard 6"
BENCH ?= /home/msears/phd/AIEplace/host/benchmarks
ARGS  ?=

ISPD2005 = adaptec1 adaptec2 adaptec3 adaptec4 bigblue1 bigblue2 bigblue3 bigblue4
ISPD2015 = $(notdir $(wildcard $(BENCH)/ispd2015/mgc_*))
MMS      = adaptec1 adaptec2 adaptec3 adaptec4 adaptec5 bigblue1 bigblue2 bigblue3 bigblue4 \
           newblue1 newblue2 newblue3 newblue4 newblue5 newblue6 newblue7

DESIGNS = $(foreach d,$(ISPD2005),--bookshelf $(BENCH)/ispd2005/$(d) $(d)) \
          $(foreach d,$(ISPD2015),--def $(BENCH)/ispd2015/$(d)/floorplan.def $(d)) \
          $(foreach d,$(MMS),--bookshelf $(BENCH)/mms/$(d) $(d))

beat_packer: beat_packer.cpp beat_packer.hpp pin_record.hpp
	g++ -std=c++17 -O2 -Wall -o $@ $<

run: beat_packer
	./beat_packer $(ARGS) $(DESIGNS)

# #42: host start-up per stage + per-phase device cycle model, chunked at the real capacity.
chunk_profile: chunk_profile.cpp beat_packer.hpp pin_record.hpp ../mailbox_widened/mailbox_layout.hpp
	g++ -std=c++17 -O2 -Wall -I. -I../mailbox_widened -o $@ $<

profile: chunk_profile
	./chunk_profile $(ARGS) $(DESIGNS)

clean:
	rm -f beat_packer chunk_profile

.PHONY: run profile clean
