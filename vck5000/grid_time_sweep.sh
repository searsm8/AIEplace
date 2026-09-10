#!/bin/bash
# Time resident-place sw_emu emulation vs PL_GRID. Build once per grid (untimed-ish),
# then a SECOND make invocation with deps satisfied -> pure emulation wall clock. Meow.
set -u
cd /home/msears/phd/AIEplace/vck5000
source /tools/Xilinx/Vitis/2022.2/settings64.sh >/dev/null 2>&1
source /opt/xilinx/xrt/setup.sh >/dev/null 2>&1
export PLATFORM_REPO_PATHS=$HOME/xilinx_local/opt/xilinx/platforms   # non-login shell skips ~/.bashrc. Meow.

OUT=/home/msears/phd/AIEplace/vck5000/grid_time_results.txt
: > "$OUT"
COMMON="TARGET=sw_emu AIE=none PL=pl_algo HOST=pl_algo BUILD_XRT=1 PLACE_ITERS=5"

for g in 512 128; do
  DEFS="-DPL_ONLY -DPL_FIELD_SOLVE -DPL_GRID=$g"
  echo "===== GRID $g : BUILD =====" | tee -a "$OUT"
  bt0=$SECONDS
  make run-resident-place $COMMON EXTRA_DEFS="$DEFS" > /tmp/grid_build_$g.log 2>&1
  bt1=$SECONDS
  echo "GRID $g build+firstrun: $((bt1-bt0)) s" | tee -a "$OUT"

  echo "===== GRID $g : TIMED EMULATION (deps satisfied) =====" | tee -a "$OUT"
  et0=$SECONDS
  make run-resident-place $COMMON EXTRA_DEFS="$DEFS" > /tmp/grid_emu_$g.log 2>&1
  et1=$SECONDS
  echo "GRID $g emulation: $((et1-et0)) s" | tee -a "$OUT"
  # pull the beacon lines + max stream depth to confirm it actually ran
  grep -E '\[resident\] (iter|ran|PASS|bootstrap)' /tmp/grid_emu_$g.log | tail -8 | tee -a "$OUT"
  echo "" | tee -a "$OUT"
done
echo "SWEEP DONE" | tee -a "$OUT"
