#!/usr/bin/env bash
# hls_loops.sh [kernel] -- summarize HLS per-loop II / latency for a pl build,
# so you don't have to type the _x_*/temp/top/.../syn/report path by hand.
#
#   ./hls_loops.sh            # default kernel hpwl_CU, newest build under build/
#   ./hls_loops.sh hpwl_CU    # explicit kernel
# Reads the per-loop *_csynth.rpt files and prints loop name, II, iteration
# latency, and trip count -- the three numbers worth scanning first.

kernel="${1:-hpwl_CU}"

# build/ lives one level up (vck5000/build), regardless of cwd.
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

# Newest matching report dir (works across sw_emu/hw_emu/hw targets).
dir=$(ls -dt "$root"/build/*/pl/*/_x_*/temp/top/top/top/solution/syn/report 2>/dev/null | head -1)
if [ -z "$dir" ]; then echo "no syn/report dir under build/ -- run 'make pl ...' first"; exit 1; fi
echo "report dir: $dir"
printf '%-26s %6s %12s %10s\n' LOOP II ITER_LATENCY TRIP

for f in "$dir/${kernel}_Pipeline_"*_csynth.rpt; do
    [ -e "$f" ] || continue
    # The loop table row looks like:  |- gather | 77 | ? | 77 | 1 | 1 | 2 ~ ? | yes |
    awk -F'|' '
        /^[[:space:]]*\|- / {
            name=$2; il=$5; ii=$6; trip=$8;
            gsub(/^[ \t]+|[ \t]+$/,"",name); gsub(/^[ \t]+|[ \t]+$/,"",il);
            gsub(/^[ \t]+|[ \t]+$/,"",ii);   gsub(/^[ \t]+|[ \t]+$/,"",trip);
            printf "%-26s %6s %12s %10s\n", name, ii, il, trip;
        }' "$f"
done
