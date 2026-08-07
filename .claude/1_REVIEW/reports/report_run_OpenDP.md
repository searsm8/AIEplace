# Report: legalize + detail-place AIEplace GP with OpenROAD opendp (Tier 1)

**Task:** `OVERNIGHT_WORK/run_OpenDP.md`
**Status:** ✅ Tier-1 (ISPD2015) flow **proven end-to-end** on `mgc_des_perf_1` — the task's "First
target." Produced a **legal** placement (OpenROAD `check_placement` passes) and the **legal-vs-legal**
HPWL vs XPlace. Reusable tooling committed. ISPD2015 generalization + ISPD2005 LEF-synthesis (Tier 2)
are documented as next steps.

## Result (mgc_des_perf_1)
| quantity | value | note |
|---|---|---|
| our **GP** HPWL | 1.1037e9 DBU (1,103,677.6 µm) | == sw_only's own GP number; ratio **1.00** vs XPlace legal (the flattering apples-to-oranges) |
| our **legal** HPWL (opendp LG+DP) | **1.1858e9 DBU** (1,185,780.6 µm) | `check_placement`: **legal** (no overlaps, on-site) |
| LG+DP inflation (ours) | **+7.4%** over our GP | OpenROAD `detailed_placement` delta |
| XPlace legalized ref | 1.1065e9 DBU | XPlace GP+LG+DP (task's `xplace_hpwl` map) |
| **our legal ÷ XPlace legal** | **1.072 (+7.2%)** | the real **legal-vs-legal** comparison |

**Takeaway (confirms the task's thesis).** The `Ratio` column in `results.csv` compares our **GP-only**
HPWL to XPlace's **legalized** HPWL, so ~1.00 flatters us. Once we run our own LG+DP, our legal HPWL is
**~7% above** XPlace's legal HPWL. Our GP started ≈ XPlace's *legalized* number (so our GP beats XPlace's
GP), but legalization then inflates our result more — consistent with [[legalizer_dp_task]] (LG raises
HPWL; DP is for legality, not wirelength). Units reconcile cleanly: DEF `UNITS/µm = 1000`, OpenROAD
reports µm, XPlace's 1.1065e9 is DBU-scale.

## How it works (the glue)
sw_only's `writeDEF` (`host/src/sw_only/src/DataBase.cpp:1007`) emits COMPONENTS + PINS + NETS but **no
`ROWS`** and writes **fractional** DBU coords — OpenROAD needs rows and integer coords. The Tier-1
bootstrap (per the handoff) sidesteps a full DEF rewrite:

1. **`tools/merge_gp_into_floorplan.py`** — splices our GP coords into the shipped `floorplan.def`
   (which already has `ROWS`/`DIEAREA`/`PINS`/`NETS` and the exact LEF names). Instance-name sets match
   exactly (112644). Rounds sw_only's fractional coords to integer DBU. Leaves fixed cells' status
   intact (mgc_des_perf_1 has none — all movable std cells).
2. **`tools/opendp_legalize.tcl`** — `read_lef` tech+cells, `read_def` the spliced DEF,
   `detailed_placement` (opendp = Abacus legalize + local detail placement), `check_placement -verbose`,
   `write_def`. Prints original/legalized HPWL.
3. **`tools/run_opendp.sh <design> <gp_def> [out_dir]`** — one-command wrapper tying 1+2 together.

Reproduce:
```
cd vck5000
tools/run_opendp.sh mgc_des_perf_1 \
  results/DSE_20260710_020637/mgc_des_perf_1/20260710_030956_296_cpu_cpu/des_perf.def \
  /home/msears/aieplace_tmp
```

## Verification
- OpenROAD `v2.0-9658` at `/usr/local/bin/openroad`; `detailed_placement` = the dpl/opendp module.
- Read: 331 lib cells, 112644 components, 112878 nets — no unknown-macro errors (names consistent).
- `detailed_placement`: total displacement 135909 µm, avg 1.2 µm, max 28.7 µm — modest, sane.
- `check_placement -verbose` on the written legal DEF → **CHECK_PLACEMENT_OK** (legal).
- Generic `run_opendp.sh` reproduces the exact numbers (1103677.6 → 1185780.6, +7%).

## Not done (next steps, explicitly deferred)
- **Generalize across ISPD2015** (task step 5): only `mgc_des_perf_1` had a ready GP DEF in
  `results/DSE_20260710_020637/`. The other `mgc_*` designs need a sw_only GP run first (produces
  `<design>.def`), then `run_opendp.sh` applies unchanged. A `placement.constraints` `maximum_utilization`
  (90.6% here) is read by sw_only as `target_density`; XPlace may run at 1.0 — a separate GP-fidelity
  caveat when reconciling absolute numbers.
- **Tier 2 (ISPD2005 adaptec/bigblue)** — bookshelf, no LEF/DEF. Needs LEF+DEF synthesis (SITE from
  `.scl`, each `.nodes` → macro, `.scl` rows → DEF ROWS, `.pl` → coords) or a bookshelf DP
  (ntuplace4dr, which XPlace itself uses). Deferred per the handoff's "do Tier 1 first."
- **Fixed macros:** mgc_des_perf_1 has none. Designs with fixed macros need the splicer to keep
  `+ FIXED` status (the script preserves the floorplan's status keyword and only rewrites coords, so it
  already handles this, but it is untested on a fixed-macro design).

## Committed artifacts
`tools/merge_gp_into_floorplan.py`, `tools/opendp_legalize.tcl`, `tools/run_opendp.sh` (3 new files).
No sw_only source change was needed for Tier 1 (the floorplan-splice bootstrap avoids extending
`writeDEF` with a `ROWS` writer — that would only be required for a self-contained DEF export).
