#######################################################################################
.PHONY: help
help:
	@echo "Makefile Usage:"
	@echo "  make build TARGET=<sw_emu/hw_emu/hw>"
	@echo "      Command to generate the design for specified Target and Device."
	@echo ""
	@echo "  make host "
	@echo "      Command to generate host."
	@echo ""
	@echo "  make device "
	@echo "      Command to generate hardware platform files(xo,xclbin)."
	@echo ""
	@echo "  make clean "
	@echo "      Command to remove the generated files."
	@echo ""
#######################################################################################
TARGET := sw_emu
PLATFORM := xilinx_vck5000_gen4x8_xdma_2_202210_1
XO := sum.$(TARGET).xo
XSA_OBJ := sum.$(TARGET).xsa
XCLBIN := sum.$(TARGET).xclbin

KERNEL_XO := device_s2mm.xo device_mm2s.xo

KERNEL_NAME := sum_kernel

AIE_OBJ := ../../aiengine/libadf.a

# Kernel compiler & linker global settings
KRNL_COMPILE_OPTS := -t $(TARGET) --config ../design.cfg --save-temps -j 26 -g
KRNL_LINK_OPTS := -t $(TARGET) --config ../link.cfg -j 26 -g --save-temps
	
.PHONY: device
.ONESHELL:
device: mkrefdir $(XO) $(XCLBIN)
	cp reference_files_$(TARGET)/$(XCLBIN) ../bin/.

mkrefdir:
	rm -Rf reference_files_$(TARGET)
	mkdir -p reference_files_$(TARGET)
	mkdir -p ../bin

# Building kernel
.PHONY: $(XO)

%.xo: src/%.cpp
	cd reference_files_$(TARGET) ; v++ $(KRNL_COMPILE_OPTS) -k $(basename $(notdir $<)) ../$< -o $@
#$(XO): src/device_m2ss.cpp src/device_s2mm.cpp
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
	rm -f *device.$(TARGET).$(PLATFORM).* *.log *.json *.xo
