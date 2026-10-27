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
# The link (kernel) clock: the fastest that has closed routing and passed on the card (230 MHz, WNS
# +0.001 ns, 2026-10-27). 200 MHz is the fallback if a source change tips it over. Meow.
KERNEL_FREQ_HZ ?= 230000000
# HLS schedules for a faster clock than the link: deeper FP and mux pipelines, every loop still II=1.
# At a 250 MHz link, HLS at 400 MHz routed WNS -0.234 ns vs -0.378 ns at HLS = link (2026-10-26). Meow.
HLS_FREQ_HZ    ?= 400000000
# opt_design otherwise promotes a high-fanout pipeline enable to a BUFG, which the platform's
# dynamic-region Pblock cannot place (DRC HDPR-18; synth/hpwl_gradient_CU/no_bufg.tcl). Meow.
LINK_OPTS_hw    = --vivado.prop run.impl_1.STEPS.OPT_DESIGN.TCL.PRE=$(abspath synth/hpwl_gradient_CU/no_bufg.tcl)

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
	$(VPP) -c -k $(KERNEL) -Isrc --hls.clock $(HLS_FREQ_HZ):$(KERNEL) $< -o $@

$(XSA): $(XO)
	$(VPP) -l $(LINK_OPTS_$(TARGET)) --clock.freqHz $(KERNEL_FREQ_HZ):$(KERNEL)_1 $< -o $@

$(XCLBIN): $(XSA)
	$(VPP) -p --package.out_dir $(HW_DIR) $< -o $@

$(HW_DIR)/emconfig.json:
	@mkdir -p $(HW_DIR)
	cd $(HW_DIR) && emconfigutil --platform $(PLATFORM) --nd 1

clean-xclbin:
	rm -rf $(HW_DIR)
