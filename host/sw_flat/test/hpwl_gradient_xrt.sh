#!/usr/bin/env bash
# hpwl_gradient_xrt.sh -- runs build/hpwl_gradient_xrt (hpwl_gradient_CU through XRT vs the same CU in g++,
# bit for bit) on the D6 config of each design, with the PL arithmetic switched on.
#
#   TARGET=sw_emu|hw_emu|hw hpwl_gradient_xrt.sh [synthetic | suite/name ...]
#     default, sw_emu and hw: synthetic (16 chunks), one small design, one with movable macros, one
#                             chunked (bigblue4, 3 chunks)
#     default, hw_emu:        synthetic only; a real benchmark is hours of RTL simulation
#
# The xclbin is pl/build/$TARGET/hpwl_gradient_CU_top.xclbin (`make -C pl xclbin TARGET=...`). Logs in
# build/hpwl_gradient_xrt_runs/$TARGET/. Exits non-zero unless every design passes. Meow.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TARGET=${TARGET:-sw_emu}
HW_DIR=$(cd "$HERE/../../../pl" && pwd)/build/$TARGET
XCLBIN=$HW_DIR/hpwl_gradient_CU_top.xclbin
CHECK=$HERE/../build/hpwl_gradient_xrt
WORK=$HERE/../build/hpwl_gradient_xrt_runs/$TARGET
DESIGNS=("$@")
if [[ ${#DESIGNS[@]} -eq 0 ]]; then
    DESIGNS=(synthetic)
    [[ $TARGET != hw_emu ]] && DESIGNS+=(ispd2015/mgc_fft_a mms/adaptec1 ispd2005/bigblue4)
fi
[[ -f $XCLBIN ]] || { echo "no $XCLBIN: run make -C pl xclbin TARGET=$TARGET"; exit 2; }
if [[ $TARGET != hw ]]; then
    export XCL_EMULATION_MODE=$TARGET EMCONFIG_PATH=$HW_DIR
fi
mkdir -p "$WORK"
failed=0
for design in "${DESIGNS[@]}"; do
    tag=${design/\//_}
    if [[ $design == synthetic ]]; then
        args=(synthetic)
    else
        python3 "$HERE/d6_config.py" "$design" "$WORK/$tag" "$WORK/$tag.toml" || { echo "FAIL $design: config"; failed=1; continue; }
        printf '\n[sw_flat]\nwa_sums = "tree"\ngrad_merge = "stream"\nhpwl = "fixed"\nwa_exp = "lut"\n' >> "$WORK/$tag.toml"
        args=("$WORK/$tag.toml")
    fi
    if (cd "$WORK" && "$CHECK" "$XCLBIN" "${args[@]}" > "$WORK/$tag.log" 2>&1); then
        echo "$design: $(tail -1 "$WORK/$tag.log")"
    else
        echo "FAIL $design (see $WORK/$tag.log):"; grep '^\[\|^FAIL' "$WORK/$tag.log" | tail -8 | sed 's/^/    /'
        failed=1
    fi
done
exit $failed
