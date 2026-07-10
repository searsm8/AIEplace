#!/usr/bin/env bash
# Tier-1 (ISPD2015) legal-vs-legal driver: splice an AIEplace GP result into the shipped
# floorplan.def, legalize + detail-place with OpenROAD opendp, and print the legal HPWL.
#
# Usage:  tools/run_opendp.sh <ispd2015_design> <our_gp_def> [out_dir]
#   e.g.  tools/run_opendp.sh mgc_des_perf_1 results/.../des_perf.def /home/msears/aieplace_tmp
# Run from vck5000/.  Requires openroad on PATH and python3.
set -euo pipefail
DESIGN=$1; GP_DEF=$2; OUT=${3:-/tmp}
BM=host/benchmarks/ispd2015/$DESIGN
mkdir -p "$OUT"
MERGED="$OUT/${DESIGN}_gp.def"; LEGAL="$OUT/${DESIGN}_legal.def"
python3 tools/merge_gp_into_floorplan.py "$GP_DEF" "$BM/floorplan.def" "$MERGED"
ORD_TECH_LEF="$BM/tech.lef" ORD_CELLS_LEF="$BM/cells.lef"   ORD_IN_DEF="$MERGED" ORD_OUT_DEF="$LEGAL"   openroad -no_init -exit tools/opendp_legalize.tcl 2>/dev/null   | grep -iE 'original HPWL|legalized HPWL|delta HPWL|LEGAL_CHECK'
echo "legal DEF: $LEGAL"
