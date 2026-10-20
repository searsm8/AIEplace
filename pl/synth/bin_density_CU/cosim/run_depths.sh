#!/usr/bin/env bash
# run_depths.sh [depth ...] -- co-simulate bin_density_CU at each forwarding depth (default 0 4 5 6), two
# at a time, and print each verdict. Depth 0 is the negative control; the depth needed is the deposit
# loop's scheduled read-to-write distance. Run from this directory with Vitis sourced. Meow.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
DEPTHS=("${@:-0 4 5 6}")
read -ra DEPTHS <<< "${DEPTHS[*]}"
one() {
    rm -rf "cosim_prj_d$1"
    FORWARD_DEPTH=$1 LIBRARY_PATH=/usr/lib/x86_64-linux-gnu vitis_hls -f cosim.tcl > "cosim_run_d$1.log" 2>&1
}
export -f one
printf '%s\n' "${DEPTHS[@]}" | xargs -P 2 -I{} bash -c 'one {}'
for d in "${DEPTHS[@]}"; do
    echo "== forwarding depth $d"
    grep -E 'bin_density_CU cosim|\*\*\* C/RTL co-simulation finished|^ERROR' "cosim_run_d$d.log" | cut -c1-240
done
