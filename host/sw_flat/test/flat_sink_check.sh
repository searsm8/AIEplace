#!/usr/bin/env bash
# flat_sink_check.sh -- run build/flat_sink_check on the 44 designs in vck5000/tools/benchmarks.py
# plus the hand-written fixtures without the legacy reader, which rejects them (or on the
# directories given, all with it), JOBS at a time (default 4; a superblue holds three parses, ~4 GB).
# One log per design in build/sink_logs/ (stderr: the readers' warnings). Exits non-zero unless
# every design printed PASS. Meow.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
CHECK=${CHECK:-$HERE/../build/flat_sink_check}
LOGS=${LOGS:-$HERE/../build/sink_logs}
JOBS=${JOBS:-4}
rm -rf "$LOGS" && mkdir -p "$LOGS"

FIXTURES=()
if [[ $# -gt 0 ]]; then
    DIRS=("$@")
else
    mapfile -t DIRS < <(cd "$REPO/vck5000/tools" && python3 -c \
        'import benchmarks; print("\n".join(sorted(benchmarks.BENCHMARKS)))' | sed "s|^|$REPO/host/benchmarks/|")
    FIXTURES=("$REPO/vck5000/test/parser/edge_cases/lefdef" "$REPO/vck5000/test/parser/edge_cases/bookshelf"
              "$HERE/fixtures/pl_fixed")
fi

check_one() {   # check_one <logs> <check> <design_dir> [flag]
    local log=$1/$(basename "$(dirname "$3")")_$(basename "$3")
    "$2" ${4:-} "$3" > "$log.txt" 2> "$log.err" || true
}
export -f check_one
printf '%s\n' "${DIRS[@]}" | xargs -P "$JOBS" -I{} bash -c 'check_one "$0" "$1" "$2"' "$LOGS" "$CHECK" {}
for fixture in "${FIXTURES[@]}"; do check_one "$LOGS" "$CHECK" "$fixture" --no-legacy; done
total=$(( ${#DIRS[@]} + ${#FIXTURES[@]} ))

passed=0
for log in "$LOGS"/*.txt; do
    grep -v '^ALL PASS\|^SOME FAILED' "$log"
    grep -q '^PASS ' "$log" && passed=$((passed + 1))
done
if [[ $passed -eq $total ]]; then
    echo "ALL PASS ($total designs)"
else
    echo "SOME FAILED ($passed of $total passed; logs in $LOGS)"; exit 1
fi
