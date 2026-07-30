#!/bin/bash
# Measure XPlace's exact overflow + HPWL on a sw_only placement DEF, WITHOUT legalizing
# (GP off, LG off, DP off -> just loads and evaluates). Robust (avoids the fragile legalizer).
# Usage: eval_overflow_xplace.sh <design> <swonly_def> [out_log]
set -e
DESIGN="$1"; DEF="$2"
OUT="${3:-/home/msears/aieplace_tmp/xpeval_${DESIGN}.log}"
REPO=/home/msears/phd/AIEplace/vck5000
PL=/home/msears/aieplace_tmp/eval_${DESIGN}.pl
python3 "$REPO/tools/def_to_bookshelf_pl.py" "$DEF" "$REPO/host/benchmarks/mms/$DESIGN/$DESIGN.pl" "$PL" >/dev/null
cd /home/msears/phd/Xplace
export CUDA_HOME=/usr/local/cuda-12.3
export PATH=/usr/local/cuda-12.3/bin:$HOME/anaconda3/bin:$PATH
$HOME/anaconda3/bin/python -u main.py --dataset mms --design_name "$DESIGN" \
  --load_from_raw True --mixed_size True --global_placement False \
  --legalization False --detail_placement False \
  --given_solution "$PL" --num_threads 8 --seed 42 > "$OUT" 2>&1 < /dev/null || true
grep -E 'Input solution' "$OUT" | head -1
