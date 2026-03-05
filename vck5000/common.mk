
PLATFORM := xilinx_vck5000_gen4x8_xdma_2_202210_1

BUILD_DIR = $(PROJECT_ROOT)/build

EXEC_CONFIG = pl$(PL)_aie$(AIE)

AIE_DIR = $(PROJECT_ROOT)/aie/src/$(AIE)
BUILD_DIR_AIE = $(BUILD_DIR)/$(TARGET)/aie
AIE_LIBADF = $(BUILD_DIR_AIE)/libadf_$(AIE).a

PL_DIR = $(PROJECT_ROOT)/pl/src/$(PL)
BUILD_DIR_PL = $(BUILD_DIR)/$(TARGET)/pl
PL_KERNELS = $(addprefix $(BUILD_DIR_PL)/, $(KERNEL_XO))

BUILD_DIR_XSA = $(BUILD_DIR)/$(TARGET)/xsa
XSA = $(BUILD_DIR_XSA)/aieplace_$(EXEC_CONFIG).xsa

BUILD_DIR_XCLBIN = $(BUILD_DIR)/$(TARGET)
XCLBIN = $(BUILD_DIR_XCLBIN)/aieplace_$(EXEC_CONFIG).xclbin

VPP_VIVADO_FLAGS = --vivado.impl.jobs 8 --vivado.synth.jobs 8
VPP_PACKAGE_FLAGS = --package.boot_mode ospi --package.out_dir $(DIR_BUILD)/$(TARGET)/package
VPP_INTERMEDIATE_FILE_DIRS = --save-temps --temp_dir $(BUILD_DIR_PL)/_x_$(EXEC_CONFIG)/temp --report_dir $(BUILD_DIR_PL)/_x_$(EXEC_CONFIG)/reports --log_dir $(BUILD_DIR_PL)/_x_$(EXEC_CONFIG)/logs

KRNL_LINK_OPTS := --config $(PL_DIR)/link.cfg --config $(PL_DIR)/design.cfg -j 26
#KRNL_LINK_OPTS += --debug.aie=true
#KRNL_LINK_OPTS += --debug.aie.chipscope TopGraph
#KRNL_LINK_OPTS += --debug.aie_trace=true --debug.aie_trace_buffer_size=0x8000

XSA_LINK_OPTS = -g $(VPP_LINK_CLOCK_FLAGS) $(VPP_PROFILE_FLAGS) $(VPP_VIVADO_FLAGS) $(VPP_CONNECTION_FLAGS) $(VPP_INTERMEDIATE_FILE_DIRS)
XCLBIN_PACKAGE_OPTS = $(VPP_PACKAGE_FLAGS) $(VPP_INTERMEDIATE_FILE_DIRS) --debug $(AIE_LIBADF)
KRNL_LINK_OPTS := --config $(PL_DIR)/link.cfg --config $(PL_DIR)/design.cfg -j 26 -g --save-temps
#KRNL_LINK_OPTS += --debug.aie=true
#KRNL_LINK_OPTS += --debug.aie.chipscope TopGraph
#KRNL_LINK_OPTS += --debug.aie_trace=true --debug.aie_trace_buffer_size=0x8000

# Ensures the target recipe dirs exist
dir_guard = @mkdir -p $(@D)


