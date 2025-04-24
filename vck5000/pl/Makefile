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
#TARGET ?= sw_emu#choose: sw_emu, hw_emu, hw
TARGET ?= hw#choose: sw_emu, hw_emu, hw
PLATFORM := xilinx_vck5000_gen4x8_xdma_2_202210_1
#XO := aieplace.$(TARGET).xo

REF_FILES := reference_files_$(TARGET)
XSA_OBJ := aieplace.$(TARGET).xsa
XCLBIN := aieplace.$(TARGET).xclbin
KERNEL_XO := partials_mm2s.xo partials_s2mm.xo
KERNEL_XO += density_mm2s.xo density_s2mm.xo

#KERNEL_NAME := sum_kernel

AIE_OBJ := ../../aie/libadf.a

# Kernel compiler & linker global settings
KRNL_COMPILE_OPTS := -t $(TARGET) --config ../design.cfg --save-temps -j 26 -g
KRNL_LINK_OPTS := -t $(TARGET) --config ../link.cfg --config ../design.cfg -j 26 -g --save-temps
#KRNL_LINK_OPTS += --debug.aie=true
#KRNL_LINK_OPTS += --debug.aie.chipscope TopGraph
#KRNL_LINK_OPTS += --debug.aie_trace=true --debug.aie_trace_buffer_size=0x8000
	
.PHONY: pl
.ONESHELL:

pl: clean mkrefdir config $(XCLBIN)
	cp $(XCLBIN) ../bin/.
	echo DONE!


# typical flow
xo: ${KERNEL_XO}
xsa: ${XSA_OBJ}# This takes awhile for hw builds!
xclbin: ${XCLBIN}


mkrefdir:
	mkdir -p reference_files_$(TARGET)
	mkdir -p ../bin

# Building kernel
%.xo: src/%.cpp
	cd ${REF_FILES}
	v++ $(KRNL_COMPILE_OPTS) --platform $(PLATFORM) -k $(basename $(notdir $<)) ../$< -o $@
	cp $@ ..
#$(XO): src/pl_kernel_m2ss.cpp src/pl_kernel_s2mm.cpp
#	cd reference_files_$(TARGET) ; v++ $(KRNL_COMPILE_OPTS) -c -k $(KERNEL_NAME) -I'$(<D)' -o'$@' ../$<
$(XSA_OBJ): $(KERNEL_XO)
	cd ${REF_FILES}
	v++ --link $(KRNL_LINK_OPTS) --platform $(PLATFORM) -o'$@' $(+) $(AIE_OBJ)
	cp $@ ..
$(XCLBIN): $(XSA_OBJ)
	cd ${REF_FILES}
	v++ --package --target $(TARGET) --platform $(PLATFORM) --save-temps --debug $(AIE_OBJ) -o $@ $^
	cp $@ ..
#--package.boot_mode=ospi

# Generates the link.cfg file which specifies connections between PL and AIE
.PHONY: config
config:
	python3 generate_link_cfg.py

.PHONY: repackage
repackage:
	cd reference_files_$(TARGET) ; v++ -p -t $(TARGET) -f $(PLATFORM) $(XSA_OBJ) $(AIE_OBJ) -o $(XCLBIN) --package.boot_mode=ospi ; cp $(XCLBIN) ../../bin/.

.PHONY: emconfig
emconfig:
	emconfigutil --platform $(PLATFORM)

# Cleaning stuff
.PHONY: clean
clean:
	rm -rf *.xo *.xsa *.xclbin *summary *log
	rm -rf reference_files_*
