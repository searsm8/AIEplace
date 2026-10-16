#!/usr/bin/env bash
# record_stream_check.sh -- runs build/record_stream_check (sw_flat's record-stream arithmetic vs
# hpwl_gradient_CU, bit for bit) on the D6 config of each design, with the PL arithmetic switched on.
#
#   record_stream_check.sh [suite/name ...]   default: two small, one unchunked large, one with movable
#                                             macros, and two chunked designs (bigblue4 with macros too)
#
# Logs in build/record_stream_runs/. Exits non-zero unless every design passes. Meow.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CHECK=$HERE/../build/record_stream_check
WORK=$HERE/../build/record_stream_runs
DESIGNS=("$@")
[[ ${#DESIGNS[@]} -eq 0 ]] && DESIGNS=(ispd2015/mgc_fft_a ispd2015/mgc_pci_bridge32_b ispd2005/adaptec1
                                      mms/adaptec1 ispd2005/bigblue4 mms/bigblue4)
mkdir -p "$WORK"
failed=0
for design in "${DESIGNS[@]}"; do
    tag=${design/\//_}
    python3 "$HERE/d6_config.py" "$design" "$WORK/$tag" "$WORK/$tag.toml" || { echo "FAIL $design: config"; failed=1; continue; }
    printf '\n[sw_flat]\nwa_sums = "tree"\ngrad_merge = "stream"\nhpwl = "fixed"\nwa_exp = "lut"\n' >> "$WORK/$tag.toml"
    if "$CHECK" "$WORK/$tag.toml" > "$WORK/$tag.log" 2>&1; then
        echo "$design: $(tail -1 "$WORK/$tag.log")"
    else
        echo "FAIL $design (see $WORK/$tag.log):"; grep -v '^\[sw_flat\]\|^sw_flat' "$WORK/$tag.log" | tail -8 | sed 's/^/    /'
        failed=1
    fi
done
exit $failed
