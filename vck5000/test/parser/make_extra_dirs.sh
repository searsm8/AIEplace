#!/usr/bin/env bash
# make_extra_dirs.sh -- extra coverage inputs for compare_parsers.sh beyond the 44 manifest
# designs. DataBase reads only a DEF named floorplan.def, so each extra DEF gets a directory of
# symlinks presenting it under that name, next to its LEFs:
#   build/extra/legal_<design>  the contest's legalized solution (after_legalized.ntup.fix.def):
#                               every component PLACED, exercising the PLACED->FIXED class rule
#   build/extra/fix_<design>    XPlace's regenerated ispd2015_fix DEF + LEFs (a different writer)
# Prints the created directories, one per line.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
OUT=$HERE/build/extra
FIX=${ISPD2015_FIX:-$HOME/phd/Xplace/data/raw/ispd2015_fix}
rm -rf "$OUT"
mkdir -p "$OUT"

link_dir() {   # link_dir <new_dir> <def> <lef>...
    local dir=$1 def=$2; shift 2
    mkdir -p "$dir"
    ln -s "$def" "$dir/floorplan.def"
    for lef in "$@"; do ln -s "$lef" "$dir/"; done
    echo "$dir"
}

for src in "$REPO"/host/benchmarks/ispd2015/*/; do
    name=$(basename "$src")
    [[ -f $src/after_legalized.ntup.fix.def ]] || continue
    link_dir "$OUT/legal_$name" "$src/after_legalized.ntup.fix.def" "$src"/*.lef
done
if [[ -d $FIX ]]; then
    for src in "$FIX"/*/; do
        name=$(basename "$src")
        link_dir "$OUT/fix_$name" "$src/$name.def" "$src"/*.lef
    done
fi
