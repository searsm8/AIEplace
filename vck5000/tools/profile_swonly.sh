#!/usr/bin/env bash
# TODO #12 Step 0 -- flat-ish profile of sw_only across designs/grids.
# Runs a fixed small iteration count per design and collects function_statistics.md.
#
#   bash tools/profile_swonly.sh <out_dir> [iters]
set -u

OUT=${1:-/tmp/mt12/profile}
ITERS=${2:-20}
ROOT=/home/msears/phd/AIEplace/vck5000
EXE=$ROOT/build/hw/host/sw_only/aieplace_sw_only.exe
BASE=$ROOT/host/src/sw_only/default_config.toml

mkdir -p "$OUT"
cd "$ROOT" || exit 1

# label:benchmark_path[:bins_per_row]
DESIGNS="
adaptec1_auto:host/benchmarks/ispd2005/adaptec1
mgc_fft_1_auto:host/benchmarks/ispd2015/mgc_fft_1
mgc_fft_1_g512:host/benchmarks/ispd2015/mgc_fft_1:512
newblue3_auto:host/benchmarks/mms/newblue3
superblue11_auto:host/benchmarks/ispd2015/mgc_superblue11_a
adaptec1_g1024:host/benchmarks/ispd2005/adaptec1:1024
adaptec1_g2048:host/benchmarks/ispd2005/adaptec1:2048
"

for entry in $DESIGNS; do
    label=${entry%%:*}
    rest=${entry#*:}
    bench=${rest%%:*}
    bins=""
    [ "$rest" != "$bench" ] && bins=${rest#*:}

    [ -d "$bench" ] || { echo "SKIP $label (no $bench)"; continue; }

    cfg=$OUT/$label.toml
    sed -e "s|^benchmark = .*|benchmark = \"$bench\"|" \
        -e "s|^convergence_min_iterations = .*|convergence_min_iterations = $ITERS|" \
        -e "s|^convergence_max_iterations = .*|convergence_max_iterations = $ITERS|" \
        -e "s|^results_dir = .*|results_dir = \"$OUT/$label\"|" \
        "$BASE" > "$cfg"
    # bins_per_row must land INSIDE [params], not appended after the last section.
    [ -n "$bins" ] && sed -i "s|^\[params\]$|[params]\nbins_per_row = $bins|" "$cfg"

    echo "=== $label ($bench${bins:+ @$bins}) ==="
    /usr/bin/time -f "wall %e s  maxRSS %M KB" "$EXE" "$cfg" > "$OUT/$label.log" 2>"$OUT/$label.time"
    cat "$OUT/$label.time"
    stats=$(find "$OUT/$label" -name function_statistics.md 2>/dev/null | head -1)
    [ -n "$stats" ] && cp "$stats" "$OUT/${label}_stats.md"
done

echo "done -> $OUT"
