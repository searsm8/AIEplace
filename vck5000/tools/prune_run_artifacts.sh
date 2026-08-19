#!/bin/bash
# Reclaim disk from results/ sweep dirs by dropping the reproducible per-run payload.
#
#   prune_run_artifacts.sh [--viz-only] [--go] <results/DIR>...
#
# Default is a dry run; pass --go to actually delete.
#
# What is SAFE to drop: the per-run payload is regenerable by re-running the sweep --
#   <sweep>/<bench>/<run>/  RowBasedPlacement.def, *.def   (output placements, up to 340 MB each)
#                           placement/iter_N.png           (per-iteration frames, GIF source)
#                           graphs/                        (plots)
#                           viz/                           (TODO #16 node-position dumps:
#                                                           ~96 MB per adaptec1 run,
#                                                           ~480 MB per bigblue4 run)
# What is NEVER dropped: every top-level file (dse_results.csv, results.md, scorecard.md,
# DSE_info) plus configs/ and analysis/.  Reports cite dse_results.csv / results.md, and
# `analyze_morris.py <DSE_dir> <morris_dir>` reads only dse_results.csv -- so a slimmed
# sweep still supports every published number and a full morris re-analysis.
#
# --viz-only drops just the TODO #16 viz/ dumps and keeps the rest of each run intact;
# use it when the placements/frames are still wanted but viz/ is the thing blowing up.
set -u

GO=0
VIZ_ONLY=0
DIRS=()
for a in "$@"; do
    case "$a" in
        --go)       GO=1 ;;
        --viz-only) VIZ_ONLY=1 ;;
        -h|--help)  sed -n '2,25p' "$0"; exit 0 ;;
        *)          DIRS+=("$a") ;;
    esac
done
[ ${#DIRS[@]} -eq 0 ] && { sed -n '2,25p' "$0"; exit 1; }

# Never prune underneath a live sweep.
if ps -eo args | grep -iE 'AIEplace_exe|dse\.py|morris\.py' | grep -v grep >/dev/null; then
    echo "!! live AIEplace/sweep process detected - refusing to prune"
    exit 1
fi

for d in "${DIRS[@]}"; do
    [ -d "$d" ] || { echo "SKIP (not a dir): $d"; continue; }
    before=$(du -sh "$d" 2>/dev/null | cut -f1)

    if [ "$VIZ_ONLY" = 1 ]; then
        mapfile -t targets < <(find "$d" -mindepth 3 -maxdepth 3 -type d -name viz)
    else
        mapfile -t targets < <(find "$d" -mindepth 1 -maxdepth 1 -type d \
                                    ! -name configs ! -name analysis)
    fi

    if [ ${#targets[@]} -eq 0 ]; then
        echo "already slim: $d ($before)"
        continue
    fi

    echo "$d ($before) -> ${#targets[@]} dir(s)"
    if [ "$GO" = 1 ]; then
        rm -rf "${targets[@]}"
        echo "   now: $(du -sh "$d" 2>/dev/null | cut -f1)"
    fi
done

[ "$GO" = 1 ] || echo && echo "(dry run - nothing deleted; add --go)"
