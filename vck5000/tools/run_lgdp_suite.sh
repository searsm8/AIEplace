#!/bin/bash
# TODO #3 -- full-pipeline evaluation: push sw_only GP results through XPlace's own
# legalizer + detailed placer (macro LG -> greedy/abacus std LG -> DP) so the headline
# number is post-DP HPWL, legal-vs-legal against XPlace's own post-DP HPWL.
#
# Stock XPlace supports this with no source changes: `--global_placement False
# --given_solution <pl>` takes the placement as given (run_placement_nesterov.py:15-25)
# and runs detail_placement_main on it, which is the same LG/DP path a full XPlace run uses.
#
#   python3 tools/gen_lgdp_inputs.py          # DEF -> bookshelf .pl for each design
#   nohup bash tools/run_lgdp_suite.sh > /tmp/lgdp/runner.log 2>&1 &
#   tail -f /tmp/lgdp/progress.txt
#
# Resumable: a design already present in the results TSV is skipped (TODO #3 pattern).
set -u

REPO=/home/msears/phd/AIEplace/vck5000
# This script lives in vck5000/tools/ (tracked, since it produces headline numbers);
# its RESULTS live in the repo-root .claude/2_ARTIFACTS/, which is gitignored. Overridable
# so a throwaway run does not touch the standing tables.
ARTIFACTS=${ARTIFACTS:-/home/msears/phd/AIEplace/.claude/2_ARTIFACTS}
XPLACE=/home/msears/phd/Xplace
PY=$HOME/anaconda3/bin/python
PL=${LGDP_PL:-/tmp/lgdp/pl}
LOG=${LGDP_LOG:-/tmp/lgdp/logs}
RES=${LGDP_RES:-$ARTIFACTS/lgdp_suite_results.tsv}
PROG=${LGDP_PROG:-/tmp/lgdp/progress.txt}

mkdir -p "$PL" "$LOG"
[ -f "$RES" ] || printf 'design\tgp_hpwl_in\tgp_ovfl_in\tmacro_lg_status\tmacro_lg_disp\tlg_hpwl\tdp_hpwl\tstatus\n' > "$RES"

run_one() {
    local d=$1 log=$LOG/${d}.log
    # Skip only a design that already SUCCEEDED -- a failed row stays in the TSV as the
    # record of the failure, but must not make the next run silently skip a retry.
    if grep -qP "^${d}\t.*\tdone$" "$RES" 2>/dev/null; then echo "SKIP $d (done in $RES)" >> "$PROG"; return; fi
    if [ ! -f "$PL/${d}.pl" ]; then echo "SKIP $d (no patched .pl)" >> "$PROG"; return; fi

    echo "START $d $(date +%H:%M:%S)" >> "$PROG"
    (
        cd "$XPLACE" || exit 1
        export CUDA_HOME=/usr/local/cuda-12.3
        export PATH=/usr/local/cuda-12.3/bin:$HOME/anaconda3/bin:$PATH
        "$PY" -u main.py --dataset mms --design_name "$d" \
            --load_from_raw True --mixed_size True --global_placement False \
            --given_solution "$PL/${d}.pl" --num_threads 8 --seed 42
    ) > "$log" 2>&1 < /dev/null
    local ec=$?

    # "Input solution, exact HPWL: 1.576476E+08 exact Overflow: 0.0190"
    local gph=$(grep -oP '(?<=Input solution, exact HPWL: )[0-9.E+-]+' "$log" | tail -1)
    local gpo=$(grep -oP '(?<=exact Overflow: )[0-9.]+' "$log" | tail -1)
    # macro LG either passes the check outright or reports a displacement
    local mst=NA mdisp=NA
    grep -q 'Check Pass in Macro Legalization' "$log" && mst=pass
    grep -q 'Check Failed\|Check failed' "$log" && mst=fail
    mdisp=$(grep -oP '(?<=Displacement = )[0-9.]+' "$log" | tail -1)
    local lgh=$(grep -oP '(?<=Finish Legalization, HPWL: )[0-9.E+-]+' "$log" | tail -1)
    local dph=$(grep -oP '(?<=After DP, HPWL: )[0-9.E+-]+' "$log" | tail -1)

    local st=done; [ $ec -ne 0 ] && st=exit$ec
    [ -z "${dph:-}" ] && st="${st}_nodp"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$d" "${gph:-NA}" "${gpo:-NA}" "$mst" "${mdisp:-NA}" "${lgh:-NA}" "${dph:-NA}" "$st" >> "$RES"
    echo "DONE $d ec=$ec dp_hpwl=${dph:-NA} $(date +%H:%M:%S)" >> "$PROG"
}

echo "RUN START $(date)" >> "$PROG"
for d in adaptec1 adaptec2 adaptec3 adaptec4 adaptec5 bigblue1 bigblue2 bigblue3 bigblue4 \
         newblue1 newblue2 newblue3 newblue4 newblue5 newblue6 newblue7; do
    run_one "$d"
done
echo "ALL DONE $(date)" >> "$PROG"
