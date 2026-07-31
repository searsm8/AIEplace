#!/usr/bin/env bash
# Compare two verify_swonly.sh artifact trees for NUMERICAL equality.
#
#   bash tools/compare_swonly.sh <ref_dir> <new_dir>
#
# Ignores two known sources of run-to-run noise that are not results:
#   - function_statistics.md (wall-clock timings)
#   - the DEF "UNITS DISTANCE MICRONS <n>" header line, which is written from an
#     uninitialized value and differs between runs of the SAME binary (pre-existing,
#     unrelated to threading -- see TODO #12 notes).
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
        if diff -q <(grep -v "^UNITS DISTANCE MICRONS" "$a") \
                   <(grep -v "^UNITS DISTANCE MICRONS" "$b") >/dev/null; then
            echo "OK       $label/$f"
        else
            n=$(diff <(grep -v "^UNITS DISTANCE MICRONS" "$a") \
                     <(grep -v "^UNITS DISTANCE MICRONS" "$b") | grep -c '^<')
            echo "DIFFER   $label/$f  ($n changed lines)"
            status=1
        fi
    done
done

[ $status -eq 0 ] && echo "== BIT-IDENTICAL ==" || echo "== DIFFERENCES FOUND =="
exit $status
