#!/usr/bin/env bash
# run_hw_profile.sh -- run the hpwl_pl HPWL-gradient harness on a real VCK5000 card, from the
# profiling build (AXI Performance Monitors instrumented -- see build/hw/HANDOFF.md Path B).
# Expects `make hw_profile` already run (on the build node) and this build/hw_profile/ tree
# copied/reachable here. Deliberately separate from run_hw.sh / build/hw so this never mixes
# outputs with another agent's build there.
set -e

XILINX_XRT=${XILINX_XRT:-/opt/xilinx/xrt}
XILINX_VITIS=${XILINX_VITIS:-/tools/Xilinx/Vitis/2022.2}
BUILD=build/hw_profile

if [ ! -f "$BUILD/hpwl_pl.hw.xclbin" ]; then
    echo "missing $BUILD/hpwl_pl.hw.xclbin -- build it first (on the build node): make hw_profile"
    exit 1
fi
if [ ! -f "$BUILD/hpwl_pl.hw.ltx" ]; then
    echo "missing $BUILD/hpwl_pl.hw.ltx -- the .xclbin alone can't address the monitor hardware," \
         "copy it in alongside the .xclbin from the build node"
    exit 1
fi

# Real hardware: XCL_EMULATION_MODE must be UNSET.
unset XCL_EMULATION_MODE
export LD_LIBRARY_PATH="$XILINX_VITIS/lib/lnx64.o:$XILINX_XRT/lib:$LD_LIBRARY_PATH"

cd "$BUILD"
./host hpwl_pl.hw.xclbin
