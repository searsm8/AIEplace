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
.DEFAULT_GOAL := all

PLATFORM := xilinx_vck5000_gen4x8_qdma_2_202220_1

BUILD_DIR = $(PROJECT_ROOT)/build

# Targets: sw_emu, hw_emu, hw
TARGET ?= hw
ifeq ($(TARGET),sw_emu)
    AIE_TARGET := x86sim
else
    AIE_TARGET := hw
endif

AIE ?= markv1
PL ?= markv1
HOST ?= sw_only

AIE_PARTIALS_INSTANCES ?= 1
AIE_DENSITY_INSTANCES ?= 1

INSTANCES_CONFIG = P$(AIE_PARTIALS_INSTANCES)D$(AIE_DENSITY_INSTANCES)
LINK_CONFIG = pl$(PL)_aie$(AIE)
EXEC_CONFIG = $(LINK_CONFIG)_$(INSTANCES_CONFIG)

AIE_DIR = $(PROJECT_ROOT)/aie/src/$(AIE)
BUILD_DIR_AIE = $(BUILD_DIR)/$(AIE_TARGET)/aie/$(AIE)
AIE_LIBADF = $(BUILD_DIR_AIE)/libadf_$(AIE)_$(INSTANCES_CONFIG).a
AIE_WORKDIR = $(BUILD_DIR_AIE)/Work_$(AIE)_$(INSTANCES_CONFIG)

# AIE=none -> PL-only design: no libadf, no PL<->AIE stream connectivity. Lets the
# xsa/xclbin flow build a kernel that doesn't use the AI Engine array.
ifeq ($(AIE),none)
AIE_LIBADF =
endif

PL_DIR = $(PROJECT_ROOT)/pl/src/$(PL)
BUILD_DIR_PL = $(BUILD_DIR)/$(TARGET)/pl/$(PL)
PL_KERNELS = $(addprefix $(BUILD_DIR_PL)/, $(KERNEL_XO))

BUILD_DIR_XSA = $(BUILD_DIR)/$(TARGET)/xsa/$(LINK_CONFIG)
XSA = $(BUILD_DIR_XSA)/aieplace_$(EXEC_CONFIG).xsa

BUILD_DIR_XCLBIN = $(BUILD_DIR)/$(TARGET)/xclbin/$(LINK_CONFIG)
XCLBIN = $(BUILD_DIR_XCLBIN)/aieplace_$(EXEC_CONFIG).xclbin

HOST_DIR = $(PROJECT_ROOT)/host/src/$(HOST)
# Parser + data model shared by every host variant (TODO #9). Compiled per-variant, because
# the variants build it with different flags (sw_only -O2/-fopenmp, pl_algo -O0), so the
# objects land in each variant's own $(BUILD_DIR_HOST)/obj.
HOST_COMMON_DIR = $(PROJECT_ROOT)/host/src/common
BUILD_DIR_HOST = $(BUILD_DIR)/$(TARGET)/host/$(HOST)
HOST_EXE = $(BUILD_DIR_HOST)/aieplace_$(HOST).exe
HOST_RUN_CONFIG ?= $(HOST_DIR)/run_config.toml

VPP_HLS_FLAGS = --hls.jobs 8
VPP_VIVADO_FLAGS = --vivado.impl.jobs 8 --vivado.synth.jobs 8
VPP_PACKAGE_FLAGS = --package.boot_mode ospi --package.out_dir $(BUILD_DIR)/$(TARGET)/package
VPP_INTERMEDIATE_FILE_DIRS = --save-temps --temp_dir $(BUILD_DIR_PL)/_x_$(EXEC_CONFIG)/temp --report_dir $(BUILD_DIR_PL)/_x_$(EXEC_CONFIG)/reports --log_dir $(BUILD_DIR_PL)/_x_$(EXEC_CONFIG)/logs

LINK_CONNECTIONS_FILE = $(BUILD_DIR_XSA)/link_$(INSTANCES_CONFIG).cfg
VPP_CONNECTION_FLAGS = --config $(LINK_CONNECTIONS_FILE)
VPP_EXTRA_DESIGN_OPTS = --config $(PL_DIR)/design.cfg
VPP_LINK_DEBUG_FLAGS = --debug.aie=true --debug.aie.chipscope TopGraph --debug.aie_trace=true --debug.aie_trace_buffer_size=0x8000
VPP_PACKAGE_DEBUG_FLAGS = --debug $(AIE_LIBADF)

# Kernel source include dir. Needed at BOTH -c and -l: in sw_emu the linker
# re-compiles the kernel from a copied source tree, so it must also see the PL
# variant's headers (host_interface.hpp / formats.hpp / modules/).
VPP_KERNEL_INCLUDE = -I$(PL_DIR)/src

# AIE=none is a PL-only build: compile out top's AIE FFT AXIS ports (they can't be left
# dangling for an RTL/hw_emu link, and a self-loop stream_connect is rejected). Gated by
# -DPL_ONLY so the HPWL/density/iteration modes still build+emulate without any AIE.
ifeq ($(AIE),none)
VPP_PL_ONLY_DEFINE = -DPL_ONLY
endif
# EXTRA_DEFS: extra -D defines for special kernel builds (e.g. small-grid field solve:
# EXTRA_DEFS="-DPL_GRID=64 -DPL_FIELD_SOLVE"). v++ -c passes them straight to HLS.
XO_COMPILE_OPTS = $(VPP_HLS_FLAGS) $(VPP_KERNEL_INCLUDE) $(VPP_EXTRA_DESIGN_OPTS) $(VPP_INTERMEDIATE_FILE_DIRS) $(VPP_PL_ONLY_DEFINE) $(EXTRA_DEFS)
XSA_LINK_OPTS = -g $(VPP_KERNEL_INCLUDE) $(VPP_LINK_CLOCK_FLAGS) $(VPP_PROFILE_FLAGS) $(VPP_VIVADO_FLAGS) $(VPP_CONNECTION_FLAGS) $(VPP_INTERMEDIATE_FILE_DIRS)
XCLBIN_PACKAGE_OPTS = $(VPP_PACKAGE_FLAGS) $(VPP_INTERMEDIATE_FILE_DIRS)

# Ensures the target recipe dirs exist
dir_guard = @mkdir -p $(@D)


