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

PROJECT_ROOT ?= $(CURDIR)/..
TARGET ?= hw

include $(PROJECT_ROOT)/common.mk

#KERNEL_NAME := sum_kernel
KERNEL_XO :=

# Kernel compiler & linker global settings
KRNL_COMPILE_OPTS := --config ../design.cfg --save-temps -j 26 -g --hls.jobs 8

.PHONY: pl
.ONESHELL:

pl: ${KERNEL_XO}

# Building kernel
$(BUILD_DIR_PL)/%.xo: src/$(PL)/src/%.cpp
	$(dir_guard)
	v++ --target $(TARGET) --platform $(PLATFORM) $(KRNL_COMPILE_OPTS) $(KRNL_INTERMEDIATE_FILE_DIRS) -k $* $< -o $@

.PHONY: emconfig
emconfig:
	emconfigutil --platform $(PLATFORM)

# Cleaning stuff
.PHONY: clean clean_all
clean:
	rm -rf $(BUILD_DIR_PL)
clean_all:
	rm -rf $(BUILD_DIR)/*/pl
