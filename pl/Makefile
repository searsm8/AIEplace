# AIEplace hardware (pl/). Targets grow as modules land (synth, cosim, hw); see README.md. Meow.
#
#   make test     tier-1: every harness in test/Makefile's HARNESSES, plain g++, seconds
#   make xclbin   hpwl_gradient_CU as a Vitis kernel: v++ -c -> -l -> -p, into build/$(TARGET)/.
#                 TARGET = sw_emu (default, minutes) | hw_emu | hw. Needs Vitis + XRT in the
#                 environment:  source ../setup_env.sh  (PATH is stripped of /mnt/ below).
#                 The host that drives it is host/sw_flat's `make hpwl-gradient-xrt`.

.PHONY: test clean xclbin clean-xclbin
test:
	$(MAKE) -C test test

clean:
	$(MAKE) -C test clean

# --- Vitis kernel build -------------------------------------------------------------------------
PLATFORM       ?= xilinx_vck5000_gen4x8_qdma_2_202220_1
TARGET         ?= sw_emu
# A baseline clock, not the goal: post-route of the predecessor closed at ~253 MHz (#41). Meow.
KERNEL_FREQ_HZ ?= 250000000

KERNEL   = hpwl_gradient_CU_top
KERNEL_SRC = synth/hpwl_gradient_CU/$(KERNEL).cpp
HW_DIR   = build/$(TARGET)
XO       = $(HW_DIR)/$(KERNEL).xo
XSA      = $(HW_DIR)/$(KERNEL).xsa
XCLBIN   = $(HW_DIR)/$(KERNEL).xclbin

# v++ -p dies under WSL when the Windows PATH (spaces) leaks into its scripts (noteToSelf). Meow.
VPP_ENV  = PATH="$$(echo "$$PATH" | tr ':' '\n' | grep -v '^/mnt/' | paste -sd:)"
VPP      = $(VPP_ENV) v++ -t $(TARGET) --platform $(PLATFORM) --temp_dir $(HW_DIR)/_x --log_dir $(HW_DIR)/logs \
           --report_dir $(HW_DIR)/reports

xclbin: $(XCLBIN) $(HW_DIR)/emconfig.json

$(XO): $(KERNEL_SRC) $(shell find src -name '*.hpp')
	@mkdir -p $(HW_DIR)
	$(VPP) -c -k $(KERNEL) -Isrc --hls.clock $(KERNEL_FREQ_HZ):$(KERNEL) $< -o $@

$(XSA): $(XO)
	$(VPP) -l --clock.freqHz $(KERNEL_FREQ_HZ):$(KERNEL)_1 $< -o $@

$(XCLBIN): $(XSA)
	$(VPP) -p --package.out_dir $(HW_DIR) $< -o $@

$(HW_DIR)/emconfig.json:
	@mkdir -p $(HW_DIR)
	cd $(HW_DIR) && emconfigutil --platform $(PLATFORM) --nd 1

clean-xclbin:
	rm -rf $(HW_DIR)
