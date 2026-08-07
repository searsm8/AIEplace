#!/usr/bin/env bash
# TODO #12 regression harness: run a fixed design set with a FIXED RNG seed and collect the
# artifacts that pin down numerical behavior, so two builds (or two thread counts) can be
# compared with a plain `diff -r`.
#
#   bash tools/verify_swonly.sh <out_dir> [iters] [set]
#     set = fast (default, ~15 s)  |  full (adds the big/slow designs)
#   env: DETERMINISTIC=false  -> force params.deterministic off (the atomics path)
#        OMP_NUM_THREADS=N    -> passed through to the placer
#        EXE=<path>           -> run a different binary (e.g. a saved pre-change reference)
#
# Collected per design, into artifacts/ (the diffable, deterministic set): iterations.dat
# (the per-iteration HPWL/overflow/step trace), RowBasedPlacement.def (every final cell
# position). The iteration is chaotic, so a single-ULP divergence at iteration 1 is visible in
# iterations.dat by iteration ~10 -- it is a sharper equality test than it looks.
# function_statistics.md (timing) is collected separately into timings/, NOT artifacts/ -- it
# carries wall-clock numbers that differ every run regardless of correctness, so `diff -r
# A/artifacts B/artifacts` would never pass with it in there (hit 2026-07-31, TODO #3).
set -u

OUT=${1:-/tmp/mt12/verify}
ITERS=${2:-20}
SET=${3:-fast}
ROOT=/home/msears/phd/AIEplace/vck5000
EXE=${EXE:-$ROOT/build/hw/host/sw_only/aieplace_sw_only.exe}
BASE=$ROOT/host/src/sw_only/default_config.toml

FAST="
adaptec1:host/benchmarks/ispd2005/adaptec1
mgc_fft_1:host/benchmarks/ispd2015/mgc_fft_1
mgc_matrix_mult_1:host/benchmarks/ispd2015/mgc_matrix_mult_1
adaptec1_g1024:host/benchmarks/ispd2005/adaptec1:1024
"
SLOW="
superblue11:host/benchmarks/ispd2015/mgc_superblue11_a
newblue3:host/benchmarks/mms/newblue3
"

DESIGNS=$FAST
[ "$SET" = full ] && DESIGNS="$FAST$SLOW"

mkdir -p "$OUT"
cd "$ROOT" || exit 1

for entry in $DESIGNS; do
    label=${entry%%:*}
    rest=${entry#*:}
    bench=${rest%%:*}
    bins=""
    [ "$rest" != "$bench" ] && bins=${rest#*:}
    [ -d "$bench" ] || { echo "SKIP $label"; continue; }

    cfg=$OUT/$label.toml
    sed -e "s|^benchmark = .*|benchmark = \"$bench\"|" \
        -e "s|^convergence_min_iterations = .*|convergence_min_iterations = $ITERS|" \
        -e "s|^convergence_max_iterations = .*|convergence_max_iterations = $ITERS|" \
        -e "s|^results_dir = .*|results_dir = \"$OUT/runs/$label\"|" \
        "$BASE" > "$cfg"
    # random_seed pins the initial placement; without it every run starts somewhere different.
    sed -i "s|^\[params\]$|[params]\nrandom_seed = 42${bins:+\nbins_per_row = $bins}|" "$cfg"
    [ -n "${DETERMINISTIC:-}" ] && sed -i "s|^deterministic = .*|deterministic = $DETERMINISTIC|" "$cfg"

    printf '%-20s ' "$label"
    start=$(date +%s.%N)
    "$EXE" "$cfg" > "$OUT/$label.log" 2>&1
    end=$(date +%s.%N)

    run=$(ls -d "$OUT/runs/$label"/*/*/ 2>/dev/null | tail -1)
    mkdir -p "$OUT/artifacts/$label" "$OUT/timings/$label"
    for f in iterations.dat RowBasedPlacement.def; do
        [ -f "$run/$f" ] && cp "$run/$f" "$OUT/artifacts/$label/"
    done
    [ -f "$run/function_statistics.md" ] && cp "$run/function_statistics.md" "$OUT/timings/$label/"
    printf 'wall %6.2f s   final: %s\n' "$(echo "$end - $start" | bc)" \
           "$(tail -1 "$OUT/artifacts/$label/iterations.dat" 2>/dev/null)"
done

echo "artifacts -> $OUT/artifacts   (compare two runs with: diff -r A/artifacts B/artifacts)"
echo "timings    -> $OUT/timings    (wall-clock only, not part of the equality test)"
