#######################################################################################
.PHONY: help
help:
	@echo "Makefile Usage:"
	@echo "  make pl TARGET=<sw_emu/hw_emu/hw>"
	@echo "      Command to generate hardware platform files(xo,xclbin)."
	@echo ""
	@echo "  make repackage"
	@echo "      Command to generate the design for specified Target and Device."
	@echo ""
	@echo "  make clean "
	@echo "      Command to remove the generated files."
	@echo ""
#######################################################################################
TARGET ?= hw#choose: sw_emu, hw_emu, hw
PLATFORM := xilinx_vck5000_gen4x8_xdma_2_202210_1
XO := aieplace.$(TARGET).xo
XSA_OBJ := aieplace.$(TARGET).xsa
XCLBIN := aieplace.$(TARGET).xclbin

KERNEL_XO := pl_kernel_s2mm.xo pl_kernel_mm2s.xo
#KERNEL_XO := pl_kernel_mm2aie2mm.xo

#KERNEL_NAME := sum_kernel

AIE_OBJ := ../../aie/libadf.a

# Kernel compiler & linker global settings
KRNL_COMPILE_OPTS := -t $(TARGET) --config ../design.cfg --save-temps -j 26 -g
KRNL_LINK_OPTS := -t $(TARGET) --config ../link.cfg -j 26 -g --save-temps
	
.PHONY: pl
.ONESHELL:

pl: mkrefdir $(XO) $(XCLBIN)
	cp reference_files_$(TARGET)/$(XCLBIN) ../bin/.

mkrefdir:
	rm -Rf reference_files_$(TARGET)
	mkdir -p reference_files_$(TARGET)
	mkdir -p ../bin

# Building kernel
.PHONY: $(XO)

%.xo: src/%.cpp
	cd reference_files_$(TARGET) ; v++ $(KRNL_COMPILE_OPTS) -k $(basename $(notdir $<)) ../$< -o $@
#$(XO): src/pl_kernel_m2ss.cpp src/pl_kernel_s2mm.cpp
#	cd reference_files_$(TARGET) ; v++ $(KRNL_COMPILE_OPTS) -c -k $(KERNEL_NAME) -I'$(<D)' -o'$@' ../$<
.PHONY: $(XSA_OBJ)
$(XSA_OBJ): $(KERNEL_XO)
	cd reference_files_$(TARGET) ; v++ $(KRNL_LINK_OPTS) -l -o'$@' $(+) $(AIE_OBJ)
.PHONY: $(XCLBIN)
$(XCLBIN): $(XSA_OBJ)
	cd reference_files_$(TARGET) ; v++ -p -t $(TARGET) -f $(PLATFORM) $^ $(AIE_OBJ) -o $@ --package.boot_mode=ospi

.PHONY: repackage
repackage:
	cd reference_files_$(TARGET) ; v++ -p -t $(TARGET) -f $(PLATFORM) $(XSA_OBJ) $(AIE_OBJ) -o $(XCLBIN) --package.boot_mode=ospi ; cp $(XCLBIN) ../../bin/.

.PHONY: emconfig
emconfig:
	emconfigutil --platform $(PLATFORM)

# Cleaning stuff
.PHONY: clean
clean:
	rm -rf reference_files_$(TARGET)