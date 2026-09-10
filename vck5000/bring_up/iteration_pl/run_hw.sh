#!/usr/bin/env bash
# run_hw.sh -- run the iteration_pl one-Nesterov-step harness on a real VCK5000 card.
# Expects the hw build already present at build/hw/ (see README, `make TARGET=hw all`).
set -e

XILINX_XRT=${XILINX_XRT:-/opt/xilinx/xrt}
XILINX_VITIS=${XILINX_VITIS:-/tools/Xilinx/Vitis/2022.2}
BUILD=build/hw

if [ ! -f "$BUILD/iteration_pl.hw.xclbin" ]; then
    echo "missing $BUILD/iteration_pl.hw.xclbin -- build it first: make TARGET=hw all"
    exit 1
fi

# Real hardware: XCL_EMULATION_MODE must be UNSET.
unset XCL_EMULATION_MODE
export LD_LIBRARY_PATH="$XILINX_VITIS/lib/lnx64.o:$XILINX_XRT/lib:$LD_LIBRARY_PATH"

cd "$BUILD"
./host iteration_pl.hw.xclbin
