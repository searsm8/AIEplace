#!/usr/bin/env bash
# best_of.sh -- minimum parse time over N runs, per design (tuning aid; run_bench.sh does all 44).
#   N=5 bash best_of.sh <binary> <suite/design> [<suite/design> ...]
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BIN=$1; shift
for design in "$@"; do
    best=999
    for ((i = 0; i < ${N:-5}; i++)); do
        t=$("$BIN" "$HERE/../../../host/benchmarks/$design" 2>/dev/null | tail -1 | awk '{print $2}')
        best=$(awk -v a="$best" -v b="$t" 'BEGIN{print (b < a) ? b : a}')
    done
    echo "$design $best"
done
