# #47 M0: the chunked-gradient cycle model, today vs the mailbox redesign.
#
#   make run [ARGS="--capacity 524288 --adders 2"]   the model on the 8 designs chunked at 1 M slots
#
# Inputs are netlist files from FlatDatabase (`make -C .. netlists`, built here on demand). CHUNKED
# lists the designs that need chunks at 1 M slots; at 512 K, pass more with DESIGNS=.
#
# `make check` is retired (2026-10-09). It held TODAY equal to the legacy vck5000 chunk_profile, both
# reading the legacy packer parser's netlist; it last passed at 9905557 (8/8). Since #45 the input is
# FlatDatabase's, by sw_only's macro rule and offset convention, which the legacy tool cannot read --
# the two now see different netlists by decision, so equality is no longer the test. Meow.

REPO    := ../../..
CXX     ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -I$(REPO)/pl/src -I../packer
NETLISTS := ../build/netlists

CHUNKED_FILES = $(addprefix $(NETLISTS)/, ispd2005_bigblue3.netlist ispd2005_bigblue4.netlist \
                ispd2015_mgc_superblue12.netlist mms_bigblue3.netlist mms_bigblue4.netlist \
                mms_newblue5.netlist mms_newblue6.netlist mms_newblue7.netlist)
CHUNKED = $(addprefix --netlist , $(CHUNKED_FILES))
DESIGNS ?= $(CHUNKED)
ARGS    ?=

.PHONY: run clean
chunk_model: chunk_model.cpp cycle_model.hpp ../packer/beat_packer.hpp ../packer/partition.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<

tail_layout: tail_layout.cpp cycle_model.hpp ../packer/beat_packer.hpp ../packer/partition.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: chunk_model $(CHUNKED_FILES)
	./chunk_model $(ARGS) $(DESIGNS)

$(NETLISTS)/%.netlist:
	$(MAKE) -C .. build/netlists/$*.netlist

clean:
	rm -f chunk_model tail_layout today.txt legacy.txt
