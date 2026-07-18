#!/bin/bash
# Legalize a sw_only MMS global-placement DEF through XPlace's own legalizer
# (macro LG + greedy/abacus std LG + DP), for an honest legal-vs-legal comparison.
#
# Usage: legalize_swonly_mms.sh <design_name> <swonly_def_path> [out_log]
# Requires the XPlace skip-GP + mixed_size macro-LG branch (run_placement_nesterov.py).
set -e
DESIGN="$1"
SWONLY_DEF="$2"
OUT_LOG="${3:-/home/msears/aieplace_tmp/xplace_swonly_lg_${DESIGN}.log}"
PL_OUT="/home/msears/aieplace_tmp/swonly_${DESIGN}.pl"

REPO=/home/msears/phd/AIEplace/vck5000
ORIG_PL="$REPO/host/benchmarks/mms/$DESIGN/$DESIGN.pl"

python3 "$REPO/tools/def_to_bookshelf_pl.py" "$SWONLY_DEF" "$ORIG_PL" "$PL_OUT"

cd /home/msears/phd/Xplace
export CUDA_HOME=/usr/local/cuda-12.3
export PATH=/usr/local/cuda-12.3/bin:$HOME/anaconda3/bin:$PATH
$HOME/anaconda3/bin/python -u main.py --dataset mms --design_name "$DESIGN" \
  --load_from_raw True --mixed_size True --global_placement False \
  --given_solution "$PL_OUT" --num_threads 8 --seed 42 > "$OUT_LOG" 2>&1 < /dev/null
echo "rc=$?"
grep -E 'Input solution|Finish Legalization|After DP' "$OUT_LOG"
