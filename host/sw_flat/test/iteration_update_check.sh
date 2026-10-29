#!/usr/bin/env bash
# iteration_update_check.sh -- runs build/iteration_update_check (iteration_update_CU vs sw_flat's
# Placer::iteration_update, bit for bit) on the D6 config of each design, with the record-stream arithmetic
# and reductions = "lanes" switched on.
#
#   iteration_update_check.sh [suite/name ...]   default: two small, movable macros, and two chunked
#                                                designs (bigblue3 K=2, bigblue4 K=3)
#
# Logs in build/iteration_update_runs/. Exits non-zero unless every design passes. Meow.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CHECK=$HERE/../build/iteration_update_check
WORK=$HERE/../build/iteration_update_runs
DESIGNS=("$@")
[[ ${#DESIGNS[@]} -eq 0 ]] && DESIGNS=(ispd2015/mgc_fft_a ispd2015/mgc_pci_bridge32_b mms/adaptec1
                                      ispd2005/bigblue3 ispd2005/bigblue4)
mkdir -p "$WORK"
failed=0
for design in "${DESIGNS[@]}"; do
    tag=${design/\//_}
    python3 "$HERE/d6_config.py" "$design" "$WORK/$tag" "$WORK/$tag.toml" || { echo "FAIL $design: config"; failed=1; continue; }
    printf '\n[sw_flat]\nwa_sums = "tree"\ngrad_merge = "stream"\nhpwl = "fixed"\nwa_exp = "lut"\nreductions = "lanes"\n' >> "$WORK/$tag.toml"
    if "$CHECK" "$WORK/$tag.toml" > "$WORK/$tag.log" 2>&1; then
        echo "$design: $(grep '^\[info\] [0-9]* chunks' "$WORK/$tag.log" | cut -c8-60) ... $(tail -1 "$WORK/$tag.log")"
    else
        echo "FAIL $design (see $WORK/$tag.log):"; grep -v '^\[sw_flat\]\|^sw_flat\|^ok' "$WORK/$tag.log" | tail -8 | sed 's/^/    /'
        failed=1
    fi
done
exit $failed
