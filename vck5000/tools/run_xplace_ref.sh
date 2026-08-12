#!/bin/bash
# XPlace full-pipeline reference runs for ispd2005 + ispd2015 (TODO: 44-design snapshot).
# MMS already has its reference (the local 2026-07-17-23:0x batch); these two suites had
# post-DP for only 2 of 28 designs, so this fills the gap.
#
# Records the four numbers the snapshot needs, per the xplace-compare skill:
#   post-GP EXACT hpwl+overflow ("After GP, best solution eval"), NOT the masked GP Stop! line
#   post-LG hpwl, post-DP hpwl
# Resumable: a design already marked done in the TSV is skipped.
set -u

XPLACE=/home/msears/phd/Xplace
# This script lives in vck5000/tools/ (tracked, since it produces headline numbers);
# its RESULTS live in the repo-root .claude/2_ARTIFACTS/, which is gitignored. Overridable
# so a throwaway run does not touch the standing tables.
ARTIFACTS=${ARTIFACTS:-/home/msears/phd/AIEplace/.claude/2_ARTIFACTS}
PY=$HOME/anaconda3/bin/python
OUT=${XREF_OUT:-/tmp/xref}
RES=${XREF_RES:-$ARTIFACTS/xplace_ref_ispd.tsv}
PROG=$OUT/progress.txt
mkdir -p "$OUT/logs"
[ -f "$RES" ] || printf 'suite\tdesign\tgp_hpwl_exact\tgp_ovfl_exact\tgp_masked_hpwl\tlg_hpwl\tdp_hpwl\titers\tstatus\n' > "$RES"

run_one() {
    # Separate statements on purpose: bash expands the whole `local a=.. b=..` line before any
    # assignment takes effect, so a later word referring to an earlier one is unset under `set -u`.
    local suite=$1
    local d=$2
    local log="$OUT/logs/${suite}_${d}.log"
    if grep -qP "^${suite}\t${d}\t.*\tdone$" "$RES" 2>/dev/null; then
        echo "SKIP $suite/$d (done)" >> "$PROG"; return
    fi
    echo "START $suite/$d $(date +%H:%M:%S)" >> "$PROG"
    (
        cd "$XPLACE" || exit 1
        export CUDA_HOME=/usr/local/cuda-12.3
        export PATH=/usr/local/cuda-12.3/bin:$HOME/anaconda3/bin:$PATH
        "$PY" -u main.py --dataset "$suite" --design_name "$d" \
            --load_from_raw True --num_threads 8 --seed 42
    ) > "$log" 2>&1 < /dev/null
    local ec=$?

    # "After GP, best solution eval, exact HPWL: X exact Overflow: Y"  <- the one to quote
    local gph=$(grep -oP '(?<=After GP, best solution eval, exact HPWL: )[0-9.E+-]+' "$log" | tail -1)
    local gpo=$(grep -oP 'After GP, best solution eval.*exact Overflow: \K[0-9.]+' "$log" | tail -1)
    local msk=$(grep -oP '(?<=GP Stop!.*masked_hpwl: )[0-9.E+-]+' "$log" | tail -1)
    local it=$(grep -oP '(?<=GP Stop! #Iters )[0-9]+' "$log" | tail -1)
    local lgh=$(grep -oP '(?<=Finish Legalization, HPWL: )[0-9.E+-]+' "$log" | tail -1)
    local dph=$(grep -oP '(?<=After DP, HPWL: )[0-9.E+-]+' "$log" | tail -1)

    local st=done
    [ $ec -ne 0 ] && st=exit$ec
    [ -z "${dph:-}" ] && st="${st}_nodp"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$suite" "$d" "${gph:-NA}" "${gpo:-NA}" "${msk:-NA}" "${lgh:-NA}" "${dph:-NA}" "${it:-NA}" "$st" >> "$RES"
    echo "DONE $suite/$d ec=$ec dp=${dph:-NA} $(date +%H:%M:%S)" >> "$PROG"
}

echo "RUN START $(date)" >> "$PROG"
for d in adaptec1 adaptec2 adaptec3 adaptec4 bigblue1 bigblue2 bigblue3 bigblue4; do
    run_one ispd2005 "$d"
done
for d in mgc_des_perf_1 mgc_des_perf_a mgc_des_perf_b mgc_edit_dist_a \
         mgc_fft_1 mgc_fft_2 mgc_fft_a mgc_fft_b \
         mgc_matrix_mult_1 mgc_matrix_mult_2 mgc_matrix_mult_a mgc_matrix_mult_b mgc_matrix_mult_c \
         mgc_pci_bridge32_a mgc_pci_bridge32_b \
         mgc_superblue11_a mgc_superblue12 mgc_superblue14 mgc_superblue16_a mgc_superblue19; do
    run_one ispd2015 "$d"
done
echo "ALL DONE $(date)" >> "$PROG"
