# makeflags.mk -- pl_algo AIE subcomponent flags.
# The density_grad FFT graph (DensityFFTGraph) needs the Vitis DSP library
# (xf::dsp::aie FFT). Use the version-matched copy bundled with the Xilinx install
# (Model_Composer xf_dsp == the Vitis 2022.2 DSPLIB). Override DSPLIB_ROOT to point
# elsewhere, e.g. a Vitis_Libraries checkout's dsp subtree (<repo>/dsp).
DSPLIB_ROOT ?= /tools/Xilinx/Model_Composer/2022.2/tps/xf_dsp

AIE_FLAGS += -include="$(XILINX_VITIS)/aietools/include"
AIE_FLAGS += -include="$(DSPLIB_ROOT)/L1/include/aie"
AIE_FLAGS += -include="$(DSPLIB_ROOT)/L1/src/aie"
AIE_FLAGS += -include="$(DSPLIB_ROOT)/L2/include/aie"
