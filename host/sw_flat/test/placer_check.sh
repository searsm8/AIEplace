#!/usr/bin/env bash
# placer_check.sh -- #45 step 5's gate: sw_flat_place must reproduce sw_only BIT FOR BIT under the
# D6 config (test/d6_config.py). Per design it runs both and requires:
#   - schedule_trace.csv identical as text (every iteration's HPWL, overflow, BB norms, gamma, lambda,
#     step, kappa at %.9e, which round-trips a float), and
#   - the same final iteration, stop reason, and final HPWL / overflow as sw_only's run report.
#
#   placer_check.sh [suite/name ...]       default: the two fast regress designs
#
# Builds sw_only first (vck5000: make host). Logs and runs in build/placer_check/. Exits non-zero
# unless every design passes. Meow.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
SW_FLAT=${SW_FLAT:-$HERE/../build/sw_flat_place}
SW_ONLY=${SW_ONLY:-$REPO/vck5000/build/hw/host/sw_only/aieplace_sw_only.exe}
WORK=$(realpath -m "${WORK:-$HERE/../build/placer_check}")   # absolute: sw_only runs from vck5000/
DESIGNS=("$@")
[[ ${#DESIGNS[@]} -eq 0 ]] && DESIGNS=(ispd2015/mgc_fft_a ispd2015/mgc_pci_bridge32_b)

make -s -C "$REPO/vck5000" host > /dev/null || { echo "FAIL: sw_only build"; exit 1; }
[[ -x "$SW_FLAT" ]] || { echo "FAIL: no $SW_FLAT (make placer)"; exit 1; }

failed=0
for design in "${DESIGNS[@]}"; do
    tag=${design/\//_}
    dir=$WORK/$tag
    rm -rf "$dir" && mkdir -p "$dir/sw_only"
    python3 "$HERE/d6_config.py" "$design" "$dir/sw_only" "$dir/d6.toml" || { echo "FAIL $design: config"; failed=1; continue; }

    start=$SECONDS
    (cd "$REPO/vck5000" && "$SW_ONLY" "$dir/d6.toml" > "$dir/sw_only.stdout" 2>&1) \
        || { echo "FAIL $design: sw_only exited non-zero (see $dir/sw_only.stdout)"; failed=1; continue; }
    only_s=$((SECONDS - start)); start=$SECONDS
    "$SW_FLAT" "$dir/d6.toml" "$dir/sw_flat" > "$dir/sw_flat.stdout" 2>&1 \
        || { echo "FAIL $design: sw_flat exited non-zero (see $dir/sw_flat.stdout)"; failed=1; continue; }
    flat_s=$((SECONDS - start))

    run=$(find "$dir/sw_only" -name schedule_trace.csv -printf '%h\n' | head -1)
    if [[ -z "$run" ]]; then echo "FAIL $design: sw_only wrote no schedule_trace.csv"; failed=1; continue; fi
    rows=$(($(wc -l < "$run/schedule_trace.csv") - 1))
    if ! cmp -s "$run/schedule_trace.csv" "$dir/sw_flat/schedule_trace.csv"; then
        first=$(diff <(cat "$run/schedule_trace.csv") "$dir/sw_flat/schedule_trace.csv" | head -1)
        echo "FAIL $design: schedule traces differ, first at $first"
        diff "$run/schedule_trace.csv" "$dir/sw_flat/schedule_trace.csv" | head -4 | cut -c1-200 | sed 's/^/      /'
        failed=1; continue
    fi

    # The final restore is a choice among traced iterations; check sw_flat made the same one and
    # measured it the same, at the report's printed precision. Meow.
    stop=$(grep -o '\[STOP\] reason=[a-z_]* iteration=[0-9]*' "$run"/*.log | head -1)
    want="[STOP] reason=$(awk '/^stop_reason/{print $2}' "$dir/sw_flat/final.txt") iteration=$(awk '/^iterations/{print $2}' "$dir/sw_flat/final.txt")"
    report() {   # report <run_summary row label> -> its value, %.3e; rows read "| |  label | value | |"
        awk -F'|' -v k="$1" '{ l = $3; gsub(/^ +| +$/, "", l); if (l == k) { v = $4; gsub(/ /, "", v); print v; exit } }' \
            "$run/run_summary.md"
    }
    mine() { printf '%.3e' "$(awk -v k="$1" '$1 == k {print $2}' "$dir/sw_flat/final.txt")"; }
    mismatch=""
    [[ "$stop" == "$want" ]] || mismatch+=" stop ('$stop' vs '$want')"
    for pair in "Final HPWL:final_hpwl" "Final HPWL (exact, all nets):final_hpwl_exact" \
                "Final Overflow (exact, no fillers):final_overflow" \
                "Final Overflow (smoothed, no fillers):final_smoothed_overflow"; do
        label=${pair%:*} key=${pair#*:}
        [[ "$(report "$label")" == "$(mine "$key")" ]] || mismatch+=" $key ($(report "$label") vs $(mine "$key"))"
    done
    if [[ -n "$mismatch" ]]; then echo "FAIL $design: trace identical but final report differs:$mismatch"; failed=1; continue; fi
    echo "PASS $design: $rows iterations bit-identical, ${stop#\[STOP\] } (sw_only ${only_s}s, sw_flat ${flat_s}s)"
done
exit $failed
