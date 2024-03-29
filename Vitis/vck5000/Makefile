# Top level Makefile which calls subdirectory Makefiles

TARGET = hw_emu # choose: hw, hw_emu, or sw_emu
AIE_GRAPH = partials # choose: aie, partials, or density

.PHONY: all aie partials density pl repackage host clean

all: $(AIE_GRAPH) pl host

aie:
	$(MAKE) -C aie

partials:
	$(MAKE) -C aie partials

density:
	$(MAKE) -C aie density

pl:
	$(MAKE) -C pl pl

repackage:
	$(MAKE) -C pl repackage

host:
	$(MAKE) -C host

clean:
	$(MAKE) -C pl clean

help:
	@echo *#*# Makefile for vck5000 implementation of AIEplace *#*#
	@echo You can build the different parts of the application these commands:
	@echo -e '\t"make aie"':'\t' Build the entire AIE array configuration. Output: libadf.a
	@echo -e '\t"make partials"': Build only the AIE kernels for partials computation.
	@echo -e '\t"make density"':'\t' Build only the AIE kernels for density computation.
	@echo -e '\t"make pl"':'\t' Build HLS kernels for the PL. Output: xilinx objects [.xo] in pl/reference_files_hw
	@echo -e '\t"make repackage"':'\t' Package the previously built PL kernels and libadf.a into finished hardware configuration. Output: bin/aieplace.hw.xclbin
	@echo -e '\t"make host"':'\t' Builds the host code which runs on a host x86 machine.