# #47 M0: the chunked-gradient cycle model, today vs the mailbox redesign.
#
#   make run [ARGS="--capacity 524288 --adders 2"]   the model on the 8 designs chunked at 1 M slots
#   make check   TODAY must equal the legacy vck5000 chunk_profile ("1-float", --unburst 1) on each
#                design, phase by phase; exits non-zero otherwise. Runs both tools (~2 min).
#
# CHUNKED lists the designs that need chunks at 1 M slots; at 512 K, pass more with DESIGNS=. Meow.

REPO    := ../../..
BENCH   := $(REPO)/host/benchmarks
CXX     ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -I$(REPO)/pl/src -I../packer
LEGACY  := $(REPO)/vck5000/bring_up/beat_packer

CHUNKED = --bookshelf $(BENCH)/ispd2005/bigblue3 bigblue3 --bookshelf $(BENCH)/ispd2005/bigblue4 bigblue4 \
          --def $(BENCH)/ispd2015/mgc_superblue12/floorplan.def mgc_superblue12 \
          --bookshelf $(BENCH)/mms/bigblue3 bigblue3 --bookshelf $(BENCH)/mms/bigblue4 bigblue4 \
          --bookshelf $(BENCH)/mms/newblue5 newblue5 --bookshelf $(BENCH)/mms/newblue6 newblue6 \
          --bookshelf $(BENCH)/mms/newblue7 newblue7
DESIGNS ?= $(CHUNKED)
ARGS    ?=

.PHONY: run check clean
chunk_model: chunk_model.cpp cycle_model.hpp ../packer/beat_packer.hpp ../packer/partition.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: chunk_model
	./chunk_model $(ARGS) $(DESIGNS)

# The total and its 9 phases on each design's today / 1-float row, compared as text. Meow.
check: chunk_model
	$(MAKE) -s -C $(LEGACY) chunk_profile
	./chunk_model $(CHUNKED) | awk '/^  today /{ $$0=gensub(/\(.*of today\) /, "", 1); print $$3, $$6, $$8, $$10, $$12, $$14, $$16, $$18, $$20, $$22 }' > today.txt
	$(LEGACY)/chunk_profile --unburst 1 $(CHUNKED) | awk '/^  1-float /{ print $$3, $$6, $$8, $$10, $$12, $$14, $$16, $$18, $$20, $$22 }' > legacy.txt
	@if [ -s today.txt ] && diff today.txt legacy.txt; then echo "PASS: TODAY equals the legacy model on $$(wc -l < today.txt) designs"; rm -f today.txt legacy.txt; \
	else echo "FAIL: TODAY differs from the legacy model (today.txt vs legacy.txt)"; exit 1; fi

clean:
	rm -f chunk_model today.txt legacy.txt
