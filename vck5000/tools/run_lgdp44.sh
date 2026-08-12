#!/bin/bash
# Push sw_only GP results for ispd2005 + ispd2015 through XPlace's own LG + DP, so the headline
# is legal-vs-legal. The MMS tier has its own runner (run_lgdp_suite.sh); this one handles the
# two tiers that need different plumbing.
#
# TWO INPUT PATHS, because the tiers are different formats:
#   ispd2005  bookshelf -> patch the .pl the design's .aux NAMES (tools/def_to_bookshelf_pl.py).
#             Reading the .aux is load-bearing: it is not always <design>.pl, and using the wrong
#             template silently drops /FIXED markers (that was the adaptec3 segfault, TODO #3).
#   ispd2015  LEF/DEF   -> patch the ORIGINAL floorplan.def's COMPONENT placements
#             (tools/def_patch_placement.py). sw_only's own DEF has no ROW statements, so handing
#             it to XPlace as THE def would lose the site rows the legalizer needs.
#
# ispd2015 splits again, on fence regions (TODO #26):
#   no REGIONS/GROUPS -> --custom_path. `--dataset ispd2015` is silently rewritten to
#                        `ispd2015_fix` (Xplace/main.py:94-96), and custom_path is checked first,
#                        so it is the only way to hand XPlace our own raw files.
#   REGIONS/GROUPS    -> --dataset ispd2015_fix. XPlace raises NotImplementedError in
#                        compute_filler_with_fence BEFORE legalization on the raw data, so the
#                        custom_path route cannot score these 9 at all. The _fix variant is
#                        XPlace's own fence-stripped data (data/fix_ispd2015_route.py), and the
#                        template patched with our placement is that variant's DEF, not
#                        floorplan.def -- patching into a DEF the run will not read scores nothing.
# Both sides of the ratio for those 9 are therefore the STRIPPED variant, which is also what the
# TCAD paper's dagger-marked numbers are. sw_only ignores fences either way (empty parser stubs).
set -u

REPO=/home/msears/phd/AIEplace/vck5000
# This script lives in vck5000/tools/ (tracked, since it produces headline numbers);
# its RESULTS live in the repo-root .claude/2_ARTIFACTS/, which is gitignored. Overridable
# so a throwaway run does not touch the standing tables.
ARTIFACTS=${ARTIFACTS:-/home/msears/phd/AIEplace/.claude/2_ARTIFACTS}
XPLACE=/home/msears/phd/Xplace
PY=$HOME/anaconda3/bin/python
GP=${LGDP44_GP:-/tmp/full44/results}
OUT=${LGDP44_OUT:-/tmp/lgdp44}
RES=${LGDP44_RES:-$ARTIFACTS/lgdp44_results.tsv}
PROG=$OUT/progress.txt
mkdir -p "$OUT/pl" "$OUT/def" "$OUT/logs"
# Last column names the DATA VARIANT the number was produced on (TODO #26 step 5): `bookshelf`,
# `ispd2015`, or `ispd2015_fix`. A fence-stripped result and a fence-carrying one are not the
# same measurement, and until this column existed the table could not say which it held.
[ -f "$RES" ] || printf 'suite\tdesign\tgp_hpwl_in\tgp_ovfl_in\tlg_hpwl\tdp_hpwl\tstatus\tvariant\n' > "$RES"

newest_def() {  # newest *.def under the design's run tree (name varies: RowBasedPlacement.def, fft.def, ...)
    find "$GP/$1/$2" -name '*.def' -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-
}

run_one() {
    local suite=$1
    local d=$2
    local log="$OUT/logs/${suite}_${d}.log"
    # `done` is no longer the last field -- the data-variant column follows it.
    if grep -qP "^${suite}\t${d}\t.*\tdone(\t|$)" "$RES" 2>/dev/null; then
        echo "SKIP $suite/$d (done)" >> "$PROG"; return
    fi

    local src
    src=$(newest_def "$suite" "$d")
    if [ -z "$src" ]; then echo "SKIP $suite/$d (no GP def)" >> "$PROG"; return; fi

    local given custom="" variant="bookshelf"
    if [ "$suite" = "ispd2005" ]; then
        local bench="$REPO/host/benchmarks/ispd2005/$d"
        local tmpl
        tmpl="$bench/$(grep -oE '[A-Za-z0-9._]+\.pl' "$bench/$d.aux" | head -1)"
        given="$OUT/pl/${d}.pl"
        python3 "$REPO/tools/def_to_bookshelf_pl.py" "$src" "$tmpl" "$given" >> "$OUT/logs/${suite}_${d}.patch.log" 2>&1 \
            || { echo "FAIL $suite/$d (pl patch)" >> "$PROG"; return; }
    else
        local raw="$XPLACE/data/raw/ispd2015/$d"
        local fix="$XPLACE/data/raw/ispd2015_fix/$d"
        local template="$raw/floorplan.def"
        given="$OUT/def/${d}.def"
        variant=ispd2015
        if grep -q '^REGIONS' "$raw/floorplan.def" 2>/dev/null; then
            # Missing _fix is the recurrence mode: a fresh box or a re-downloaded benchmark set
            # leaves it absent, and silently skipping 9 designs is what sent TODO #22 round the
            # loop twice. Fail loudly, on stdout as well as the progress file, naming the command.
            if [ ! -f "$fix/$d.def" ]; then
                echo "FAIL $suite/$d: no ispd2015_fix data. Regenerate it with:" | tee -a "$PROG"
                echo "    cd $XPLACE/data && python3 fix_ispd2015_route.py" | tee -a "$PROG"
                return
            fi
            # Raw newer than derived = the benchmarks were re-downloaded and _fix is stale.
            if [ "$raw/floorplan.def" -nt "$fix/$d.def" ]; then
                echo "WARNING $suite/$d: $fix/$d.def is OLDER than the raw floorplan.def -- regenerate" | tee -a "$PROG"
            fi
            template="$fix/$d.def"
            variant=ispd2015_fix
        fi
        python3 "$REPO/tools/def_patch_placement.py" "$src" "$template" "$given" \
            >> "$OUT/logs/${suite}_${d}.patch.log" 2>&1 \
            || { echo "FAIL $suite/$d (def patch)" >> "$PROG"; return; }
        if [ "$variant" = "ispd2015" ]; then
            custom="--custom_path tech_lef:$raw/tech.lef,cell_lef:$raw/cells.lef,def:$raw/floorplan.def,design_name:$d,benchmark:ispd2015"
        fi
    fi

    echo "START $suite/$d ($variant) $(date +%H:%M:%S)" >> "$PROG"
    (
        cd "$XPLACE" || exit 1
        export CUDA_HOME=/usr/local/cuda-12.3
        export PATH=/usr/local/cuda-12.3/bin:$HOME/anaconda3/bin:$PATH
        if [ -n "$custom" ]; then
            "$PY" -u main.py $custom --load_from_raw True --global_placement False \
                --given_solution "$given" --num_threads 8 --seed 42
        elif [ "$variant" = "ispd2015_fix" ]; then
            "$PY" -u main.py --dataset ispd2015_fix --design_name "$d" --load_from_raw True \
                --global_placement False --given_solution "$given" --num_threads 8 --seed 42
        else
            "$PY" -u main.py --dataset ispd2005 --design_name "$d" --load_from_raw True \
                --global_placement False --given_solution "$given" --num_threads 8 --seed 42
        fi
    ) > "$log" 2>&1 < /dev/null
    local ec=$?

    local gph gpo lgh dph st
    gph=$(grep -oP '(?<=Input solution, exact HPWL: )[0-9.E+-]+' "$log" | tail -1)
    gpo=$(grep -oP 'Input solution.*exact Overflow: \K[0-9.]+' "$log" | tail -1)
    lgh=$(grep -oP '(?<=Finish Legalization, HPWL: )[0-9.E+-]+' "$log" | tail -1)
    dph=$(grep -oP '(?<=After DP, HPWL: )[0-9.E+-]+' "$log" | tail -1)
    st=done; [ $ec -ne 0 ] && st=exit$ec
    [ -z "${dph:-}" ] && st="${st}_nodp"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$suite" "$d" "${gph:-NA}" "${gpo:-NA}" "${lgh:-NA}" "${dph:-NA}" "$st" "$variant" >> "$RES"
    echo "DONE $suite/$d ec=$ec dp=${dph:-NA} $(date +%H:%M:%S)" >> "$PROG"
}

echo "RUN START $(date)" >> "$PROG"
for d in adaptec1 adaptec2 adaptec3 adaptec4 bigblue1 bigblue2 bigblue3 bigblue4; do
    run_one ispd2005 "$d"
done
for d in $(ls "$XPLACE/data/raw/ispd2015"); do
    run_one ispd2015 "$d"
done
echo "ALL DONE $(date)" >> "$PROG"
