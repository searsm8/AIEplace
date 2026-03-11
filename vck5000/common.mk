# Common variables to be included by all makefiles in the AIEPlace project
#
########################################################################################
# # Include this file as follows:
# PROJECT_ROOT ?= <point to the root path of the AIEPlace git repo)
# include $(PROJECT_ROOT)/common.mk
#
# # It's important to understand the difference between `=` and `:=` in this file
# # '='  (recursive / lazy expansion)
# #      The right-hand side is expanded only when the variable is used.
# # ':=' (simple / immediate expansion)
# #      The right-hand side is expanded immediately when the variable is defined.
# # Example:
# A = hello
# B = $(A) world
#
# C = hello
# D := $(C) world
#
# A = hi
# C = hi
#
# # Result when used later (eg. in a recipe):
# #   $(B) -> "hi world"      (because A is evaluated later)
# #   $(D) -> "hello world"   (because C was expanded immediately)
########################################################################################

PLATFORM := xilinx_vck5000_gen4x8_qdma_2_202220_1

BUILD_DIR = $(PROJECT_ROOT)/build

# Targets: sw_emu, hw_emu, hw
TARGET ?= hw
ifeq ($(TARGET),sw_emu)
    AIE_TARGET := x86sim
else
    AIE_TARGET := hw
endif

AIE ?= mark_v1
PL ?= mark_v1
HOST ?= mark_v1

AIE_PARTIALS_INSTANCES ?= 1
AIE_DENSITY_INSTANCES ?= 1

INSTANCES_CONFIG = P$(AIE_PARTIALS_INSTANCES)D$(AIE_DENSITY_INSTANCES)
EXEC_CONFIG = pl$(PL)_aie$(AIE)_$(INSTANCES_CONFIG)

AIE_DIR = $(PROJECT_ROOT)/aie/src/$(AIE)
BUILD_DIR_AIE = $(BUILD_DIR)/$(AIE_TARGET)/aie/$(AIE)
AIE_LIBADF = $(BUILD_DIR_AIE)/libadf_$(AIE)_$(INSTANCES_CONFIG).a
AIE_WORKDIR = $(BUILD_DIR_AIE)/Work_$(AIE)_$(INSTANCES_CONFIG)

PL_DIR = $(PROJECT_ROOT)/pl/src/$(PL)
BUILD_DIR_PL = $(BUILD_DIR)/$(TARGET)/pl/$(PL)
PL_KERNELS = $(addprefix $(BUILD_DIR_PL)/, $(KERNEL_XO))

BUILD_DIR_XSA = $(BUILD_DIR)/$(TARGET)/xsa
XSA = $(BUILD_DIR_XSA)/aieplace_$(EXEC_CONFIG).xsa

BUILD_DIR_XCLBIN = $(BUILD_DIR)/$(TARGET)
XCLBIN = $(BUILD_DIR_XCLBIN)/aieplace_$(EXEC_CONFIG).xclbin

HOST_DIR = $(PROJECT_ROOT)/host/src/$(HOST)
BUILD_DIR_HOST = $(BUILD_DIR)/$(TARGET)/host
HOST_EXE = $(BUILD_DIR_HOST)/aieplace_$(HOST).exe

VPP_HLS_FLAGS = --hls.jobs 8
VPP_VIVADO_FLAGS = --vivado.impl.jobs 8 --vivado.synth.jobs 8
VPP_PACKAGE_FLAGS = --package.boot_mode ospi --package.out_dir $(BUILD_DIR)/$(TARGET)/package
VPP_INTERMEDIATE_FILE_DIRS = --save-temps --temp_dir $(BUILD_DIR_PL)/_x_$(EXEC_CONFIG)/temp --report_dir $(BUILD_DIR_PL)/_x_$(EXEC_CONFIG)/reports --log_dir $(BUILD_DIR_PL)/_x_$(EXEC_CONFIG)/logs

VPP_CONNECTION_FLAGS = --config $(BUILD_DIR_PL)/link_$(INSTANCES_CONFIG).cfg
VPP_EXTRA_DESIGN_OPTS = --config $(PL_DIR)/design.cfg
VPP_LINK_DEBUG_FLAGS = --debug.aie=true --debug.aie.chipscope TopGraph --debug.aie_trace=true --debug.aie_trace_buffer_size=0x8000
VPP_PACKAGE_DEBUG_FLAGS = --debug $(AIE_LIBADF)

XO_COMPILE_OPTS = $(VPP_HLS_FLAGS) $(VPP_EXTRA_DESIGN_OPTS)
XSA_LINK_OPTS = -g $(VPP_LINK_CLOCK_FLAGS) $(VPP_PROFILE_FLAGS) $(VPP_VIVADO_FLAGS) $(VPP_CONNECTION_FLAGS) $(VPP_INTERMEDIATE_FILE_DIRS)
XCLBIN_PACKAGE_OPTS = $(VPP_PACKAGE_FLAGS) $(VPP_INTERMEDIATE_FILE_DIRS)

# Ensures the target recipe dirs exist
dir_guard = @mkdir -p $(@D)


