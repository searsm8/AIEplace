# setup_env.sh -- environment for building/running AIEplace (Vitis 2022.2, VCK5000).
#
# SOURCE this, don't execute it:   source setup_env.sh
# From .bashrc (interactive):      source /home/msears/phd/AIEplace/setup_env.sh
# In a non-interactive shell:      bash -lc "source .../setup_env.sh && make ..."
#
# It is the single place the build-relevant vars live, so the synth/emulation
# flows behave the same whether invoked by hand, from .bashrc, or by tooling.
#
# Machine-specific values are marked [MACHINE]; override them by exporting before
# sourcing, e.g.  XILINX_ROOT=/opt/Xilinx source setup_env.sh

# --- tool roots ------------------------------------------------------------- [MACHINE]
: "${XILINX_VERSION:=2022.2}"
: "${XILINX_ROOT:=/tools/Xilinx}"
: "${XILINX_XRT:=/opt/xilinx/xrt}"
# Platforms (the .xpfm for xilinx_vck5000_gen4x8_qdma_2_202220_1 lives here).
: "${PLATFORM_REPO_PATHS:=$HOME/xilinx_local/opt/xilinx/platforms}"
export PLATFORM_REPO_PATHS

# --- Vitis + XRT settings --------------------------------------------------------
# settings64.sh sets XILINX_VITIS, puts v++/vitis_analyzer on PATH, etc.
source "${XILINX_ROOT}/Vitis/${XILINX_VERSION}/settings64.sh"
source "${XILINX_XRT}/setup.sh"

# --- emulation host-run gotcha ---------------------------------------------------
# A sw_emu/hw_emu host needs the Vitis emulation shims (lnx64.o) and the XRT libs
# on LD_LIBRARY_PATH, or the host aborts at xclbin load. Harmless for hw builds.
export LD_LIBRARY_PATH="${XILINX_VITIS}/lib/lnx64.o:${XILINX_XRT}/lib:${LD_LIBRARY_PATH}"

echo "[setup_env] Vitis ${XILINX_VERSION} @ ${XILINX_ROOT}  |  XRT ${XILINX_XRT}"
echo "[setup_env] PLATFORM_REPO_PATHS=${PLATFORM_REPO_PATHS}"
