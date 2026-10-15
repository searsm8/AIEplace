#!/usr/bin/env bash
# overflow_map_check.sh -- run build/overflow_map_check on the designs in vck5000/tools/benchmarks.py (or
# the ones named), each from its D6 config with the grid capped at density_map_CU's 1024, JOBS at a time
# (default 4), ITERATIONS in (default 30; a large value runs to the stop). One log per design in
# build/overflow_map_logs/. Exits non-zero unless every design printed PASS. Meow.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
CHECK=${CHECK:-$HERE/../build/overflow_map_check}
LOGS=$(realpath -m "${LOGS:-$HERE/../build/overflow_map_logs}")
JOBS=${JOBS:-4}
ITERATIONS=${ITERATIONS:-30}
rm -rf "$LOGS" && mkdir -p "$LOGS"

if [[ $# -gt 0 ]]; then DESIGNS=("$@")
else mapfile -t DESIGNS < <(cd "$REPO/vck5000/tools" && python3 -c 'import benchmarks; print("\n".join(sorted(benchmarks.BENCHMARKS)))')
fi
[[ ${#DESIGNS[@]} -gt 0 ]] || { echo "FAIL: no designs"; exit 1; }

check_one() {   # check_one <logs> <check> <here> <iterations> <suite/name>
    local tag=${5/\//_}
    python3 "$3/d6_config.py" "$5" "$1/$tag.sw_only" "$1/$tag.toml" 1024 \
        && "$2" "$1/$tag.toml" "$4" > "$1/$tag.txt" 2> "$1/$tag.err" || echo "FAIL $5: did not run" >> "$1/$tag.txt"
}
export -f check_one
printf '%s\n' "${DESIGNS[@]}" | xargs -P "$JOBS" -I{} bash -c 'check_one "$0" "$1" "$2" "$3" "$4"' "$LOGS" "$CHECK" "$HERE" "$ITERATIONS" {}

passed=0
for log in "$LOGS"/*.txt; do
    grep -v '^ok' "$log" | sed "s|^|$(basename "$log" .txt): |"
    grep -q '^PASS ' "$log" && passed=$((passed + 1))
done
echo "overflow_map_check: $passed of ${#DESIGNS[@]} designs PASS"
[[ $passed -eq ${#DESIGNS[@]} ]]
