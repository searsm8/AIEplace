#!/usr/bin/env bash
# run_hw.sh -- build (if needed) and run the Dhar HPWL-gradient kernel on the REAL VCK5000.
#
# Real hardware (TARGET=hw) runs on the card, NOT in WSL emulation. Per the project notes only the
# machine with the physical VCK5000 (Geert's) can execute this; sw_emu is the WSL path (`make run`).
# Unlike emulation, a hardware run must NOT set XCL_EMULATION_MODE.
#
# The v++ hw build takes hours, so it is skipped when the xclbin already exists -- delete it (or
# `make TARGET=hw clean`) to force a rebuild. Pass an optional net count as $1 (default: host's own).
#
# Usage:
#   ./run_hw.sh            # build hw xclbin if missing, then run + verify on the card
#   ./run_hw.sh 4000       # same, with a 4000-net design
set -euo pipefail

cd "$(dirname "$0")"

VITIS_SETTINGS=${VITIS_SETTINGS:-/tools/Xilinx/Vitis/2022.2/settings64.sh}
XRT_SETUP=${XRT_SETUP:-/opt/xilinx/xrt/setup.sh}
KERNEL=hpwl_gradient_dhar_top
XCLBIN=${KERNEL}.hw.xclbin
NUM_NETS=${1:-}

echo "== sourcing toolchain =="
# shellcheck disable=SC1090
source "$VITIS_SETTINGS"
# shellcheck disable=SC1090
source "$XRT_SETUP"

# v++ resolves the platform via PLATFORM_REPO_PATHS. Interactive shells get it from ~/.bashrc; a
# fresh/non-login shell does not, so fall back to the repo path that ~/.bashrc itself exports.
export PLATFORM_REPO_PATHS="${PLATFORM_REPO_PATHS:-$HOME/xilinx_local/opt/xilinx/platforms}"

if [[ ! -f "$XCLBIN" ]]; then
    echo "== $XCLBIN not found -- building for hardware (this takes hours) =="
    make TARGET=hw all
else
    echo "== reusing existing $XCLBIN (delete it to force a rebuild) =="
    make TARGET=hw host          # ensure the host binary is current
fi

echo "== running on hardware =="
# No XCL_EMULATION_MODE for real hardware. XRT + Vitis emulation libs still needed on the path.
LD_LIBRARY_PATH="${XILINX_VITIS}/lib/lnx64.o:${XILINX_XRT}/lib:${LD_LIBRARY_PATH:-}" \
    ./host "$XCLBIN" ${NUM_NETS:+"$NUM_NETS"}
