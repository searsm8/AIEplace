#!/usr/bin/env bash
# Compare two verify_swonly.sh artifact trees for NUMERICAL equality.
#
#   bash tools/compare_swonly.sh <ref_dir> <new_dir>
#
# Ignores the one known source of run-to-run noise that is not a result:
#   - function_statistics.md (wall-clock timings)
set -u

REF=${1:?ref dir}/artifacts
NEW=${2:?new dir}/artifacts
status=0

for d in "$REF"/*/; do
    label=$(basename "$d")
    for f in iterations.dat RowBasedPlacement.def; do
        a=$REF/$label/$f ; b=$NEW/$label/$f
        [ -f "$a" ] || continue
        if [ ! -f "$b" ]; then echo "MISSING  $label/$f"; status=1; continue; fi
        if diff -q "$a" "$b" >/dev/null; then
            echo "OK       $label/$f"
        else
            n=$(diff "$a" "$b" | grep -c '^<')
            echo "DIFFER   $label/$f  ($n changed lines)"
            status=1
        fi
    done
done

[ $status -eq 0 ] && echo "== BIT-IDENTICAL ==" || echo "== DIFFERENCES FOUND =="
exit $status
