#!/usr/bin/env bash
# compare_parsers.sh -- the #43 equivalence gate: parse every design with the native reader AND
# with Limbo, dump every parse-derived DataBase field, and require the two dumps byte-identical.
# Exits non-zero if any design differs or fails to parse.
#
#   bash compare_parsers.sh                    # the 44 designs in tools/benchmarks.py, the
#                                              # extra DEFs make_extra_dirs.sh sets up, and edge_cases/
#   bash compare_parsers.sh <dir> [<dir> ...]  # specific design directories
#
# JOBS designs run at once (default 4; each holds two parsed designs' dumps, up to ~2 GB RSS per
# parse on the largest). Dumps go to build/dumps and are deleted on a match; on a mismatch both are
# kept and the first differing lines are printed.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
JOBS=${JOBS:-4}
mkdir -p "$HERE/build/dumps"

if [[ $# -gt 0 ]]; then
    DIRS=("$@")
else
    mapfile -t DIRS < <(cd "$REPO/vck5000/tools" && python3 -c \
        'import benchmarks; print("\n".join(sorted(benchmarks.BENCHMARKS)))' | sed "s|^|$REPO/host/benchmarks/|")
    mapfile -t -O "${#DIRS[@]}" DIRS < <(bash "$HERE/make_extra_dirs.sh")
    DIRS+=("$HERE/edge_cases/lefdef" "$HERE/edge_cases/bookshelf")
fi

compare_one() {
    local here=$1 dir=${2%/}
    local tag native limbo
    tag=$(basename "$(dirname "$dir")")_$(basename "$dir")
    native=$here/build/dumps/$tag.native.txt
    limbo=$here/build/dumps/$tag.limbo.txt
    if ! "$here/build/parse_bench" --dump "$native" "$dir" > /dev/null 2>&1; then
        echo "FAIL $tag: native reader failed"; return 1
    fi
    if ! "$here/build/parse_bench_limbo" --limbo --dump "$limbo" "$dir" > /dev/null 2>&1; then
        echo "FAIL $tag: Limbo failed"; return 1
    fi
    # Limbo's default callbacks exit(0) mid-parse (see LefBridge::lef_nondefault_cbk): status 0,
    # no dump. Meow.
    if [[ ! -f $limbo ]]; then
        echo "FAIL $tag: Limbo exited without finishing the parse"; return 1
    fi
    if cmp -s "$native" "$limbo"; then
        echo "PASS $tag ($(wc -l < "$native") lines identical)"
        rm -f "$native" "$limbo"
    else
        echo "FAIL $tag: dumps differ"
        diff "$native" "$limbo" | head -10
        return 1
    fi
}
export -f compare_one

printf '%s\n' "${DIRS[@]}" | xargs -P "$JOBS" -I{} bash -c 'compare_one "$0" "$1"' "$HERE" {} | sort -k2
status=${PIPESTATUS[1]}
[[ $status -eq 0 ]] && echo "ALL PASS (${#DIRS[@]} designs)" || echo "SOME FAILED"
exit $status
