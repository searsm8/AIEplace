#!/bin/bash
# Full-suite sw_only runner, any tier. Configs from tools/gen_suite_configs.py
# (named "<suite>_<design>.toml" -- a bare design name collides across suites).
#
#   python3 tools/gen_suite_configs.py --suites ispd2005 ispd2015 \
#       --outdir /tmp/full44/configs --results-root /tmp/full44/results
#   setsid nohup bash tools/run_suite.sh > /tmp/full44/runner.log 2>&1 < /dev/null &
#   tail -f /tmp/full44/progress.txt
#
# SEQUENTIAL on purpose. The placer is already OpenMP-threaded across all cores, so two
# concurrent runs just split the same cores and hit the master-thread spin cliff
# (memory `openmp-thread-count-cliff`); concurrency was measured and rejected for dse.py
# sweeps in commit 9bea10e. One run at a time IS the max-throughput configuration here.
#
# Resumable: a design already marked done in the TSV is skipped.
set -u

REPO=/home/msears/phd/AIEplace/vck5000
# This script lives in vck5000/tools/ (tracked, since it produces headline numbers);
# its RESULTS live in the repo-root .claude/2_ARTIFACTS/, which is gitignored. Overridable
# so a throwaway run does not touch the standing tables.
ARTIFACTS=${ARTIFACTS:-/home/msears/phd/AIEplace/.claude/2_ARTIFACTS}
EXE=$REPO/build/hw/host/sw_only/aieplace_sw_only.exe
CFG=${SUITE_CFG:-/tmp/full44/configs}
LOG=${SUITE_LOG:-/tmp/full44/logs}
RES=${SUITE_RES:-$ARTIFACTS/full44_suite_results.tsv}
PROG=${SUITE_PROG:-/tmp/full44/progress.txt}

mkdir -p "$LOG"
[ -f "$RES" ] || printf 'suite\tdesign\thpwl_exact\thpwl_masked\tovfl_smoothed\tovfl_exact\tovfl_macro_excluded\titers\tstop_reason\tphase1_iters\truntime_s\tstatus\n' > "$RES"

num() { grep -oE '[0-9]+\.[0-9]+e[+-][0-9]+|[0-9]+\.[0-9]+' | tail -1; }

run_one() {
    local cfg=$1
    local base
    base=$(basename "$cfg" .toml)          # "<suite>_<design>"
    local suite=${base%%_*}
    local d=${base#*_}
    local log="$LOG/${base}.log"

    if grep -qP "^${suite}\t${d}\t.*\tdone$" "$RES" 2>/dev/null; then
        echo "SKIP $suite/$d (done)" >> "$PROG"; return
    fi
    echo "START $suite/$d $(date +%H:%M:%S)" >> "$PROG"
    local t0=$SECONDS
    "$EXE" "$cfg" > "$log" 2>&1
    local ec=$?
    local secs=$((SECONDS - t0))

    local hpwl_e hpwl_m ovs ove ovm it reason p1it st
    hpwl_e=$(grep 'Final HPWL (exact' "$log" | num)
    hpwl_m=$(grep -E '\| Final HPWL +\|' "$log" | num)
    ovs=$(grep 'Final Overflow (smoothed' "$log" | num)
    ove=$(grep 'Final Overflow (exact' "$log" | num)
    ovm=$(grep -i 'Macro-Excluded Overflow' "$log" | num)
    it=$(grep -oP '(?<=iteration=)[0-9]+' "$log" | tail -1)
    reason=$(grep -oP '(?<=reason=)[a-z_]+' "$log" | tail -1)
    p1it=$(grep '\[PHASE\]' "$log" | grep -oP '(?<=end_iteration=)[0-9]+')
    st=done; [ $ec -ne 0 ] && st=exit$ec

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$suite" "$d" "${hpwl_e:-NA}" "${hpwl_m:-NA}" "${ovs:-NA}" "${ove:-NA}" "${ovm:-NA}" \
        "${it:-NA}" "${reason:-NA}" "${p1it:-none}" "$secs" "$st" >> "$RES"
    echo "DONE $suite/$d ec=$ec hpwl=${hpwl_e:-NA} iters=${it:-NA} ${secs}s $(date +%H:%M:%S)" >> "$PROG"
}

echo "RUN START $(date)  binary md5 $(md5sum "$EXE" | cut -d' ' -f1)" >> "$PROG"
# Smallest DESIGN first (by benchmark directory size, not config size -- the configs are all
# ~5 KB), so a config or parsing error surfaces in seconds instead of after the first
# multi-hour design, and the cheap results land early if the run is interrupted.
order_by_design_size() {
    local c base suite d dir
    for c in "$CFG"/*.toml; do
        base=$(basename "$c" .toml); suite=${base%%_*}; d=${base#*_}
        dir="$REPO/host/benchmarks/$suite/$d"
        echo "$(du -sb "$dir" 2>/dev/null | cut -f1 || echo 0) $c"
    done | sort -n | cut -d' ' -f2-
}
for cfg in $(order_by_design_size); do
    run_one "$cfg"
done
echo "ALL DONE $(date)" >> "$PROG"
