#!/usr/bin/env bash
# slot_layout_check.sh -- run build/slot_layout_check on the 44 designs in vck5000/tools/benchmarks.py,
# each at its target_density column, JOBS at a time (default 4; a superblue holds two parses and its
# chunked encode). One log per design in build/layout_logs/. Exits non-zero unless every design
# printed PASS. Meow.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
CHECK=${CHECK:-$HERE/../build/slot_layout_check}
LOGS=${LOGS:-$HERE/../build/layout_logs}
JOBS=${JOBS:-4}
rm -rf "$LOGS" && mkdir -p "$LOGS"

# "<suite>/<name> <target_density>" per line. Meow.
mapfile -t DESIGNS < <(cd "$REPO/vck5000/tools" && python3 -c \
    'import benchmarks as b; print("\n".join(p + " " + str(b.BENCHMARKS[p]["target_density"]) for p in sorted(b.BENCHMARKS)))')
if [[ ${#DESIGNS[@]} -eq 0 ]]; then echo "FAIL: no designs from benchmarks.py"; exit 1; fi

check_one() {   # check_one <logs> <check> <repo> "<suite>/<name> <td>"
    local path=${4% *} density=${4#* }
    "$2" "$3/host/benchmarks/$path" "$density" > "$1/${path/\//_}.txt" 2> "$1/${path/\//_}.err" || true
}
export -f check_one
printf '%s\n' "${DESIGNS[@]}" | xargs -P "$JOBS" -I{} bash -c 'check_one "$0" "$1" "$2" "$3"' "$LOGS" "$CHECK" "$REPO" {}

passed=0
for log in "$LOGS"/*.txt; do
    cat "$log"
    grep -q '^PASS ' "$log" && passed=$((passed + 1))
done
if [[ $passed -eq ${#DESIGNS[@]} ]]; then
    echo "ALL PASS (${#DESIGNS[@]} designs)"
else
    echo "SOME FAILED ($passed of ${#DESIGNS[@]} passed; logs in $LOGS)"; exit 1
fi
