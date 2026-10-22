#!/usr/bin/env bash
# compare_parsers.sh -- the #43 equivalence gate: parse every design with the native reader, dump
# every parse-derived DataBase field, and require the dump's sha256 to equal the frozen hash of
# what Limbo produced for the same input (limbo_dump_hashes.txt, recorded 2026-10-22 just before
# Limbo was removed). Exits non-zero if any design differs, fails to parse, or has no frozen hash.
#
#   bash compare_parsers.sh                    # the 44 designs in tools/benchmarks.py, the
#                                              # extra DEFs make_extra_dirs.sh sets up, and edge_cases/
#   bash compare_parsers.sh <dir> [<dir> ...]  # specific design directories
#
# JOBS designs run at once (default 4; up to ~2 GB RSS per parse on the largest). Dumps go to
# build/dumps and are deleted on a match; on a mismatch the dump is kept for inspection.
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
    local tag native expected actual
    tag=$(basename "$(dirname "$dir")")_$(basename "$dir")
    native=$here/build/dumps/$tag.native.txt
    expected=$(awk -v t="$tag" '$2 == t { print $1 }' "$here/limbo_dump_hashes.txt")
    if [[ -z $expected ]]; then
        echo "FAIL $tag: no frozen Limbo hash for this input"; return 1
    fi
    if ! "$here/build/parse_bench" --dump "$native" "$dir" > /dev/null 2>&1; then
        echo "FAIL $tag: native reader failed"; return 1
    fi
    actual=$(sha256sum < "$native" | cut -d' ' -f1)
    if [[ $actual == "$expected" ]]; then
        echo "PASS $tag ($(wc -l < "$native") lines match Limbo's hash)"
        rm -f "$native"
    else
        echo "FAIL $tag: dump differs from Limbo's (kept at $native)"
        return 1
    fi
}
export -f compare_one

printf '%s\n' "${DIRS[@]}" | xargs -P "$JOBS" -I{} bash -c 'compare_one "$0" "$1"' "$HERE" {} | sort -k2
status=${PIPESTATUS[1]}
[[ $status -eq 0 ]] && echo "ALL PASS (${#DIRS[@]} designs)" || echo "SOME FAILED"
exit $status
