#!/usr/bin/env bash
# TODO #12 speedup measurement: pre-threading binary vs current, in both reduction modes.
#
#   bash tools/bench_swonly.sh <ref_exe> <out_dir> [iters] [set]
#
# Reports total performIteration time from function_statistics.md, NOT wall clock: a 20-40
# iteration benchmark spends 1-3 s in the (serial, unthreaded) LEF/DEF read, which would
# swamp the thing being measured. Arms are interleaved per design so a drifting machine load
# hits all three roughly equally -- but for a number worth quoting, run it on an IDLE box.
set -u

REF=${1:?path to the reference (pre-threading) executable}
OUT=${2:-/tmp/mt12/bench}
ITERS=${3:-40}
SET=${4:-fast}
ROOT=/home/msears/phd/AIEplace/vck5000
NEW=$ROOT/build/hw/host/sw_only/aieplace_sw_only.exe
BASE=$ROOT/host/src/sw_only/run_config.toml

FAST="
adaptec1:host/benchmarks/ispd2005/adaptec1
adaptec1_g1024:host/benchmarks/ispd2005/adaptec1:1024
mgc_matrix_mult_1:host/benchmarks/ispd2015/mgc_matrix_mult_1
"
SLOW="
superblue11:host/benchmarks/ispd2015/mgc_superblue11_a
newblue3:host/benchmarks/mms/newblue3
"
DESIGNS=$FAST
[ "$SET" = full ] && DESIGNS="$FAST$SLOW"

mkdir -p "$OUT"
cd "$ROOT" || exit 1

# total microseconds recorded for one function in a run directory's stats table
stat_us() { awk -F'|' -v k="$2" 'NR>2{gsub(/ /,"",$2); gsub(/ /,"",$4); if($2==k) print $4}' "$1"; }

run_arm() {  # exe, cfg, tag -> echoes performIteration us
    local exe=$1 cfg=$2 tag=$3
    rm -rf "$OUT/runs/$tag"
    "$exe" "$cfg" > "$OUT/$tag.log" 2>&1
    local s
    s=$(find "$OUT/runs/$tag" -name function_statistics.md 2>/dev/null | tail -1)
    [ -n "$s" ] && stat_us "$s" performIteration || echo 0
}

printf '%-20s %12s %12s %12s   %8s %8s\n' design ref_us det_us fast_us det_x fast_x
for entry in $DESIGNS; do
    label=${entry%%:*}; rest=${entry#*:}; bench=${rest%%:*}
    bins=""; [ "$rest" != "$bench" ] && bins=${rest#*:}
    [ -d "$bench" ] || continue

    for mode in ref det fast; do
        cfg=$OUT/${label}_$mode.toml
        sed -e "s|^benchmark = .*|benchmark = \"$bench\"|" \
            -e "s|^convergence_min_iterations = .*|convergence_min_iterations = $ITERS|" \
            -e "s|^convergence_max_iterations = .*|convergence_max_iterations = $ITERS|" \
            -e "s|^results_dir = .*|results_dir = \"$OUT/runs/${label}_$mode\"|" "$BASE" > "$cfg"
        sed -i "s|^\[params\]$|[params]\nrandom_seed = 42${bins:+\nbins_per_row = $bins}|" "$cfg"
        [ "$mode" = fast ] && sed -i "s|^deterministic = .*|deterministic = false|" "$cfg"
    done

    r=$(run_arm "$REF" "$OUT/${label}_ref.toml"  "${label}_ref")
    d=$(run_arm "$NEW" "$OUT/${label}_det.toml"  "${label}_det")
    f=$(run_arm "$NEW" "$OUT/${label}_fast.toml" "${label}_fast")
    awk -v l="$label" -v r="$r" -v d="$d" -v f="$f" \
        'BEGIN{printf "%-20s %12d %12d %12d   %7.2fx %7.2fx\n", l, r, d, f, (d?r/d:0), (f?r/f:0)}'
done
