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

# Every header the kernel pulls in. WITHOUT THIS the .xo depends only on the .cpp, so editing
# a module header (where all the datapath actually lives) leaves make saying "Nothing to be
# done" and the stale .xo in place -- and any synthesis result read afterwards describes the
# PREVIOUS code. That silently invalidated two measurements on 2026-08-28 before it was caught.
# design.cfg is included too: it is a v++ --config input, so it changes the build.
PL_KERNEL_DEPS := $(wildcard src/$(PL)/src/*.hpp) \
                  $(wildcard src/$(PL)/src/modules/*.hpp) \
                  $(wildcard src/$(PL)/design.cfg)

# Building kernel
$(BUILD_DIR_PL)/%.xo: src/$(PL)/src/%.cpp $(PL_KERNEL_DEPS)
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
