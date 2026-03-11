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

# In each subproject, add the .xo files to the makeflags.mk file
KERNEL_XO :=

include $(PROJECT_ROOT)/common.mk
include $(PL_DIR)/makeflags.mk


.PHONY: all
all: $(PL_KERNELS)

# Building kernel
$(BUILD_DIR_PL)/%.xo: src/$(PL)/src/%.cpp
	$(dir_guard)
	v++ -c --target $(TARGET) --platform $(PLATFORM) $(XO_COMPILE_OPTS) -k $* $< -o $@

.PHONY: emconfig
emconfig:
	emconfigutil --platform $(PLATFORM)

# Cleaning stuff
.PHONY: clean clean_all
clean:
	rm -rf $(BUILD_DIR_PL)
clean_all:
	rm -rf $(BUILD_DIR)/*/pl
