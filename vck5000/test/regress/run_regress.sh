#!/usr/bin/env bash
# sw_only regression tripwire (TODO #17).
#
# Runs sw_only on a frozen config and asserts the result is BIT-IDENTICAL to a committed
# baseline. `params.deterministic = true` makes sw_only reproducible at any thread count, so
# this compares exactly -- there is no tolerance to argue about and nothing to flake.
#
#   cd vck5000 && make test-regress                 # all designs
#   test/regress/run_regress.sh mgc_fft_a           # one design
#   test/regress/run_regress.sh --update-baselines --reason "..."
#
# This is tier 2 by cost (tens of seconds), so it is deliberately NOT part of `make test` --
# that suite's value is being cheap enough to run after every edit. See AIEplace/CLAUDE.md
# § Verification Loop and test/regress/README.md.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REGRESS_DIR="$PROJECT_ROOT/test/regress"
WORK_DIR="$PROJECT_ROOT/test/regress/work"
AIEPLACE_EXE="${AIEPLACE_EXE:-$PROJECT_ROOT/build/hw/host/sw_only/aieplace_sw_only.exe}"

update=0
slow=0
reason=""
designs=()

while [ $# -gt 0 ]; do
    case "$1" in
        --update-baselines) update=1 ;;
        --slow)             slow=1 ;;
        --reason)           reason="${2:-}"; shift ;;
        -h|--help)          sed -n '2,16p' "${BASH_SOURCE[0]}"; exit 0 ;;
        -*)                 echo "unknown option: $1" >&2; exit 2 ;;
        *)                  designs+=("$1") ;;
    esac
    shift
done

# A baseline may only be rewritten with a stated reason, and the reason is recorded in the file.
# The failure mode this guards against is "the test failed so I refreshed the baseline" -- an
# unexplained baseline commit is indistinguishable from a silently accepted regression.
if [ "$update" -eq 1 ] && [ -z "$reason" ]; then
    echo "ERROR: --update-baselines requires --reason \"why the expected result changed\"." >&2
    echo "       The reason is written into the baseline header and shows up in the git diff." >&2
    exit 2
fi

if [ ! -x "$AIEPLACE_EXE" ]; then
    echo "ERROR: sw_only executable not found: $AIEPLACE_EXE" >&2
    echo "       Build it with: cd vck5000 && make host HOST=sw_only" >&2
    exit 2
fi

# configs/       -- fast tier, seconds per design, the default set
# configs/slow/  -- minutes per design (mixed-size: phase 2 + macro legalization), opt-in
if [ ${#designs[@]} -eq 0 ]; then
    for cfg in "$REGRESS_DIR"/configs/*.toml; do
        designs+=("$(basename "$cfg" .toml)")
    done
    if [ "$slow" -eq 1 ]; then
        for cfg in "$REGRESS_DIR"/configs/slow/*.toml; do
            designs+=("$(basename "$cfg" .toml)")
        done
    fi
fi

cd "$PROJECT_ROOT"   # every path in a frozen config is relative to vck5000/

commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
failures=0

for design in "${designs[@]}"; do
    config="$REGRESS_DIR/configs/$design.toml"
    [ -f "$config" ] || config="$REGRESS_DIR/configs/slow/$design.toml"
    baseline="$REGRESS_DIR/baselines/$design.baseline"
    printf '\n=== %s ===\n' "$design"

    if [ ! -f "$config" ]; then
        echo "FAIL: no frozen config at test/regress/configs[/slow]/$design.toml"
        failures=$((failures + 1))
        continue
    fi

    # Clear previous run output, but only the DIRECTORIES -- the per-design .stdout logs stay so
    # that a failure in an earlier design is still diagnosable after a later one has run. Clearing
    # by directory rather than by name matters because the placer names its output after the
    # BENCHMARK (mms/adaptec1 -> work/adaptec1/), which need not match the config's name.
    mkdir -p "$WORK_DIR"
    find "$WORK_DIR" -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
    start=$SECONDS
    if ! "$AIEPLACE_EXE" "$config" > "$WORK_DIR/$design.stdout" 2>&1; then
        echo "FAIL: placer exited non-zero -- see test/regress/work/$design.stdout"
        failures=$((failures + 1))
        continue
    fi
    elapsed=$((SECONDS - start))

    # <results_dir>/<benchmark>/<timestamp>_<methods>/ -- depth 2, and the dir was just cleared,
    # so exactly one such directory exists.
    run_dir="$(find "$WORK_DIR" -mindepth 2 -maxdepth 2 -type d | head -1)"
    iterations="$run_dir/iterations.dat"
    def_out="$(find "$run_dir" -maxdepth 1 -name '*.def' | head -1)"

    if [ ! -f "$iterations" ] || [ ! -f "$def_out" ]; then
        echo "FAIL: run produced no iterations.dat / .def -- see test/regress/work/$design.stdout"
        failures=$((failures + 1))
        continue
    fi

    # Final positions of every real cell, at the DEF's own precision. Strictly stronger than the
    # 4-significant-figure trajectory: it catches drift too small to move a printed HPWL digit.
    sha="$(sha256sum "$def_out" | cut -d' ' -f1)"
    n_iters="$(grep -c '^[0-9]' "$iterations")"

    if [ "$update" -eq 1 ]; then
        {
            echo "# sw_only regression baseline -- $design"
            echo "#"
            echo "# Produced by:  test/regress/run_regress.sh --update-baselines"
            echo "# Config:       ${config#"$PROJECT_ROOT/"}  (frozen; changes with this file)"
            echo "# Recorded:     $(date -u +%Y-%m-%d) at commit $commit"
            echo "# Reason:       $reason"
            echo "#"
            echo "# DO NOT refresh this file to make a failing test pass. Read the diff first: the"
            echo "# body below is the full per-iteration trajectory, so the row where it starts to"
            echo "# differ tells you what changed and when. See test/regress/README.md."
            echo "positions_sha256 $sha"
            echo "---"
            cat "$iterations"
        } > "$baseline"
        echo "baseline written: $n_iters iterations, ${elapsed}s, sha ${sha:0:12}"
        continue
    fi

    if [ ! -f "$baseline" ]; then
        echo "FAIL: no baseline at test/regress/baselines/$design.baseline"
        echo "      Create it with: $0 --update-baselines --reason \"initial baseline\" $design"
        failures=$((failures + 1))
        continue
    fi

    baseline_sha="$(awk '/^positions_sha256 /{print $2}' "$baseline")"
    ok=1

    if ! diff -q <(sed -n '/^---$/,$p' "$baseline" | tail -n +2) "$iterations" > /dev/null; then
        ok=0
        echo "FAIL: trajectory differs from baseline. First divergence:"
        diff <(sed -n '/^---$/,$p' "$baseline" | tail -n +2) "$iterations" | head -6 | sed 's/^/      /'
    fi

    # A sha mismatch with an IDENTICAL trajectory is the interesting case: the placement moved by
    # less than the trajectory's 4 printed significant figures. Say so, rather than just "differs".
    if [ "$sha" != "$baseline_sha" ]; then
        if [ "$ok" -eq 1 ]; then
            echo "FAIL: trajectory matches but final positions differ (sha256"
            echo "      ${baseline_sha:0:12} -> ${sha:0:12}) -- drift below the printed precision."
        else
            echo "FAIL: final positions also differ (sha256 ${baseline_sha:0:12} -> ${sha:0:12})."
        fi
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        echo "PASS: $n_iters iterations, trajectory and final positions bit-identical (${elapsed}s)"
    else
        failures=$((failures + 1))
    fi
done

echo
if [ "$update" -eq 1 ]; then
    # A run that failed to produce output wrote no baseline, so it must not report success --
    # otherwise a broken build looks like a successful refresh.
    if [ "$failures" -ne 0 ]; then
        echo "FAIL: $failures design(s) produced no result; their baselines were NOT written."
        exit 1
    fi
    echo "Baselines updated. READ THE GIT DIFF before committing -- an unexplained baseline"
    echo "change is indistinguishable from an accepted regression."
    exit 0
elif [ "$failures" -eq 0 ]; then
    echo "PASS: sw_only matches every committed baseline."
    exit 0
else
    echo "FAIL: $failures design(s) drifted from baseline."
    echo "If the change was intended, regenerate with:"
    echo "  test/regress/run_regress.sh --update-baselines --reason \"<why>\""
    exit 1
fi
