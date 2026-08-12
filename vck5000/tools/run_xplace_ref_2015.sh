#!/bin/bash
# XPlace reference runs for ISPD2015, via --custom_path.
#
# WHY custom_path and not --dataset ispd2015: main.py:94-96 silently rewrites
# `--dataset ispd2015` to `ispd2015_fix` ("We haven't yet support fence region in ispd2015"),
# and data/raw/ispd2015_fix/ holds only mgc_pci_bridge32_b -- so every other design dies with
# "Design Name X should in ['mgc_pci_bridge32_b']". find_design_params checks args.custom_path
# BEFORE the dataset dispatch, so custom_path bypasses the rewrite and reads our own files.
#
# TWO PATHS, split on whether the design carries fence regions (TODO #26):
#   no REGIONS/GROUPS -> --custom_path, reading our own benchmark files, as described above.
#   REGIONS/GROUPS    -> --dataset ispd2015_fix, XPlace's OWN fence-stripped variant, which
#                        data/fix_ispd2015_route.py generates from the same raw files. XPlace
#                        raises NotImplementedError in compute_filler_with_fence on the raw data,
#                        so this is the only variant on which it produces a number at all -- and
#                        it is the variant the TCAD paper's 9 dagger-marked designs were run on.
# Regenerate the _fix data with:  cd ~/phd/Xplace/data && python3 fix_ispd2015_route.py
set -u

XPLACE=/home/msears/phd/Xplace
# This script lives in vck5000/tools/ (tracked, since it produces headline numbers);
# its RESULTS live in the repo-root .claude/2_ARTIFACTS/, which is gitignored. Overridable
# so a throwaway run does not touch the standing tables.
ARTIFACTS=${ARTIFACTS:-/home/msears/phd/AIEplace/.claude/2_ARTIFACTS}
PY=$HOME/anaconda3/bin/python
RAW=$XPLACE/data/raw/ispd2015
FIX=$XPLACE/data/raw/ispd2015_fix
OUT=${XREF_OUT:-/tmp/xref}
RES=${XREF_RES:-$ARTIFACTS/xplace_ref_ispd.tsv}
PROG=$OUT/progress2015.txt
mkdir -p "$OUT/logs"

run_one() {
    local d=$1
    local log="$OUT/logs/ispd2015_${d}.log"
    # `done` is no longer the last field -- the data-variant column follows it.
    if grep -qP "^ispd2015\t${d}\t.*\tdone(\t|$)" "$RES" 2>/dev/null; then
        echo "SKIP $d (done)" >> "$PROG"; return
    fi
    # Fence regions -> the raw data is unrunnable for XPlace; take its own _fix variant instead.
    local nregions source
    nregions=$(grep -c '^REGIONS' "$RAW/$d/floorplan.def" 2>/dev/null || echo 0)
    # Missing _fix is the recurrence mode: a fresh box or a re-downloaded benchmark set leaves it
    # absent, and a quiet `blocked_fence_region` row is what sent TODO #22 round the loop twice.
    if [ "$nregions" -ne 0 ] && [ ! -f "$FIX/$d/$d.def" ]; then
        printf 'ispd2015\t%s\tNA\tNA\tNA\tNA\tNA\tNA\tblocked_fence_region\tNA\n' "$d" >> "$RES"
        echo "BLOCKED $d: no ispd2015_fix data. Regenerate it with:" | tee -a "$PROG"
        echo "    cd $XPLACE/data && python3 fix_ispd2015_route.py" | tee -a "$PROG"
        return
    fi
    # Raw newer than derived = the benchmarks were re-downloaded and _fix is stale.
    if [ "$nregions" -ne 0 ] && [ "$RAW/$d/floorplan.def" -nt "$FIX/$d/$d.def" ]; then
        echo "WARNING $d: $FIX/$d/$d.def is OLDER than the raw floorplan.def -- regenerate" | tee -a "$PROG"
    fi
    if [ "$nregions" -ne 0 ]; then source=ispd2015_fix; else source=ispd2015; fi

    echo "START $d ($source) $(date +%H:%M:%S)" >> "$PROG"
    (
        cd "$XPLACE" || exit 1
        export CUDA_HOME=/usr/local/cuda-12.3
        export PATH=/usr/local/cuda-12.3/bin:$HOME/anaconda3/bin:$PATH
        if [ "$source" = "ispd2015_fix" ]; then
            "$PY" -u main.py --dataset ispd2015_fix --design_name "$d" \
                --load_from_raw True --num_threads 8 --seed 42
        else
            "$PY" -u main.py \
                --custom_path "tech_lef:$RAW/$d/tech.lef,cell_lef:$RAW/$d/cells.lef,def:$RAW/$d/floorplan.def,design_name:$d,benchmark:ispd2015" \
                --load_from_raw True --num_threads 8 --seed 42
        fi
    ) > "$log" 2>&1 < /dev/null
    local ec=$?

    local gph gpo msk it lgh dph st
    gph=$(grep -oP '(?<=After GP, best solution eval, exact HPWL: )[0-9.E+-]+' "$log" | tail -1)
    gpo=$(grep -oP 'After GP, best solution eval.*exact Overflow: \K[0-9.]+' "$log" | tail -1)
    msk=$(grep -oP 'masked_hpwl: \K[0-9.E+-]+' "$log" | tail -1)
    it=$(grep -oP '(?<=GP Stop! #Iters )[0-9]+' "$log" | tail -1)
    lgh=$(grep -oP '(?<=Finish Legalization, HPWL: )[0-9.E+-]+' "$log" | tail -1)
    dph=$(grep -oP '(?<=After DP, HPWL: )[0-9.E+-]+' "$log" | tail -1)
    st=done; [ $ec -ne 0 ] && st=exit$ec
    [ -z "${dph:-}" ] && st="${st}_nodp"

    # Last column names the DATA VARIANT, not the run: a fence-stripped number and a
    # fence-carrying one are not the same measurement (TODO #26 step 5).
    printf 'ispd2015\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$d" "${gph:-NA}" "${gpo:-NA}" "${msk:-NA}" "${lgh:-NA}" "${dph:-NA}" "${it:-NA}" "$st" "$source" >> "$RES"
    echo "DONE $d ec=$ec dp=${dph:-NA} $(date +%H:%M:%S)" >> "$PROG"
}

# Drop the exit1 rows this suite's first (--dataset) attempt left behind, and the
# blocked_fence_region placeholders, so the resume check retries what the _fix data now unblocks.
if [ -f "$RES" ]; then
    grep -vP '^ispd2015\t.*\t(exit1_nodp|blocked_fence_region)$' "$RES" > "$RES.tmp" && mv "$RES.tmp" "$RES"
fi

echo "RUN START $(date)" >> "$PROG"
for d in $(ls "$RAW"); do run_one "$d"; done
echo "ALL DONE $(date)" >> "$PROG"
