#!/usr/bin/env bash
# Time parse_bench on every design in tools/benchmarks.py. Each design is parsed REPEATS times
# (default 2) and the minimum is kept, so the number is the warm-page-cache parse, not the disk.
# Usage: run_bench.sh <bench_binary> [extra args passed to the binary]
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
BIN=$1; shift
REPEATS=${REPEATS:-2}
DESIGNS=$(cd "$REPO/vck5000/tools" && python3 -c 'import benchmarks; print("\n".join(sorted(benchmarks.BENCHMARKS)))')
for design in $DESIGNS; do
    best=""
    for ((i = 0; i < REPEATS; i++)); do
        line=$("$BIN" "$@" "$REPO/host/benchmarks/$design" 2>/dev/null | tail -1)
        secs=$(echo "$line" | awk '{print $2}')
        if [[ -z $best ]] || awk "BEGIN{exit !($secs < $best)}"; then best=$secs; fi
    done
    echo "$design $best"
done
