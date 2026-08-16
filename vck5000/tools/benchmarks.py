# benchmarks.py
# ----------------------------------------------------------------------------
# MASTER BENCHMARK MANIFEST — the single source of truth for which designs we
# work on. A design is on this list iff it is (a) physically present in the
# XPlace dataset (~/phd/Xplace/data/raw), (b) has a tuned config in XPlace's
# setup_dataset.py, and (c) present locally under host/benchmarks/. Efforts are
# limited to this list so every sw_only result has a comparable XPlace reference.
#
# Grid column = XPlace's REQUESTED num_bin (setup_dataset.py). XPlace then caps it at num_rows —
# num_bin_y = 2^floor(log2(num_rows)) when num_rows < requested (database.py:161) — and so does
# sw_only, at run time, for BOTH the auto formula and an explicit override (Setup.cpp, the row_cap
# branch; TODO #31). So carry the *requested* value here and let the code cap it; 13 of 20 tier-2
# designs actually run at 128/256. The effective grid is logged per run ("effective bins_per_row=").
# Fixing the cap-on-override closed the low-row overflow stalls AND reconciled our overflow signal
# with XPlace's (both evaluate at the capped grid).
#
# target_density column = the per-design value dse.py feeds as maximum_utilization. It MATCHES
# XPlace's: XPlace sets the same per-design td in setup_dataset.py (setup_design_args, the mgc_*
# branches) and logs it as "target density = 0.65" (database.py:839) -- the "target_density: 1.0"
# that appears in XPlace logs is a params-dict echo, NOT the effective value. (Corrects the earlier
# "#25 -- XPlace uses 1.0" claim, which misread that echo; verified 0 mismatches on all 20 ISPD2015.)
#
# Tiers:
#   1  ISPD2005  — fixed macros; coordinate frame MATCHES sw_only (site_width=100
#                  cancels the bookshelf read-scale) -> honest apples-to-apples
#                  HPWL ratio. The anchor set for any "vs XPlace" claim.
#   2  ISPD2015  — std-cell + pre-placed macros; XPlace HPWL is site-width-
#      (mgc_*)     normalized (~/site_width, e.g. /200) -> a DIFFERENT frame; an
#                  XPlace reference must be converted (masked_hpwl*site_width)
#                  before the ratio is meaningful. Tier 1+2 == the 28-design
#                  XPlace-paper suite (== dse.py _full_suite).
#   3  MMS        — mixed-size / MOVABLE macros (--mixed_size). A different
#                  regime (where the preconditioner matters); bookshelf/raw
#                  frame like ISPD2005 but unlegalized + macros move. Separate
#                  track from the paper's non-macro comparison.
#
# Explicitly OUT of scope (do not add): ISPD2019 (we have it locally but XPlace
# has no data for it) and the classic superblue1-18 (XPlace-tuned but no data
# present). See memory morris_sobol_sensitivity_tooling / grid_sizing.
# ----------------------------------------------------------------------------

# (design_name, suite, tier, effective_grid, our_target_density)   # see header note on both columns
_ROWS = [
    # --- Tier 1: ISPD2005 (fixed macros, clean frame) -----------------------
    ("adaptec1", "ispd2005", 1,  512, 1.0),
    ("adaptec2", "ispd2005", 1, 1024, 1.0),
    ("adaptec3", "ispd2005", 1, 1024, 1.0),
    ("adaptec4", "ispd2005", 1, 1024, 1.0),
    ("bigblue1", "ispd2005", 1,  512, 1.0),
    ("bigblue2", "ispd2005", 1, 1024, 1.0),
    ("bigblue3", "ispd2005", 1, 2048, 1.0),
    ("bigblue4", "ispd2005", 1, 2048, 1.0),

    # --- Tier 2: ISPD2015 mgc_* (site-width frame) --------------------------
    # grid = XPlace's REQUESTED num_bin (setup_dataset.py: 512 for every mgc_*). sw_only caps it at
    # num_rows at run time (Setup.cpp, XPlace database.py:161), so the low-row designs actually run
    # at 128/256 -- do NOT pre-cap the value here; the code does it and logs the effective grid.
    ("mgc_des_perf_1",     "ispd2015", 2, 512, 0.910),
    ("mgc_des_perf_a",     "ispd2015", 2, 512, 0.429),
    ("mgc_des_perf_b",     "ispd2015", 2, 512, 0.497),
    ("mgc_edit_dist_a",    "ispd2015", 2, 512, 0.455),
    ("mgc_fft_1",          "ispd2015", 2, 512, 0.835),
    ("mgc_fft_2",          "ispd2015", 2, 512, 0.650),
    ("mgc_fft_a",          "ispd2015", 2, 512, 0.500),
    ("mgc_fft_b",          "ispd2015", 2, 512, 0.600),
    ("mgc_matrix_mult_1",  "ispd2015", 2, 512, 0.802),
    ("mgc_matrix_mult_2",  "ispd2015", 2, 512, 0.800),
    ("mgc_matrix_mult_a",  "ispd2015", 2, 512, 0.600),
    ("mgc_matrix_mult_b",  "ispd2015", 2, 512, 0.600),
    ("mgc_matrix_mult_c",  "ispd2015", 2, 512, 0.600),
    ("mgc_pci_bridge32_a", "ispd2015", 2, 512, 0.384),
    ("mgc_pci_bridge32_b", "ispd2015", 2, 512, 0.143),
    ("mgc_superblue11_a",  "ispd2015", 2, 512, 0.650),
    ("mgc_superblue12",    "ispd2015", 2, 512, 0.650),  # XPlace requests 512, not 1024 (was a manifest error)
    ("mgc_superblue14",    "ispd2015", 2, 512, 0.560),
    ("mgc_superblue16_a",  "ispd2015", 2, 512, 0.550),
    ("mgc_superblue19",    "ispd2015", 2, 512, 0.530),

    # --- Tier 3: MMS (mixed-size, movable macros) ---------------------------
    ("adaptec1", "mms", 3,  512, 1.0),
    ("adaptec2", "mms", 3, 1024, 1.0),
    ("adaptec3", "mms", 3, 1024, 1.0),
    ("adaptec4", "mms", 3, 1024, 1.0),
    ("adaptec5", "mms", 3, 1024, 0.5),
    ("bigblue1", "mms", 3,  512, 1.0),
    ("bigblue2", "mms", 3, 1024, 1.0),
    ("bigblue3", "mms", 3, 2048, 1.0),
    ("bigblue4", "mms", 3, 2048, 1.0),
    ("newblue1", "mms", 3,  512, 0.8),
    ("newblue2", "mms", 3, 1024, 0.9),
    ("newblue3", "mms", 3, 2048, 0.8),
    ("newblue4", "mms", 3, 1024, 0.5),
    ("newblue5", "mms", 3, 1024, 0.5),
    ("newblue6", "mms", 3, 2048, 0.8),
    ("newblue7", "mms", 3, 2048, 0.8),
]

# XPlace MMS reference, design -> (mixed_gp_hpwl, mixed_gp_overflow).
#
# READ THIS BEFORE COMPARING. XPlace's mixed-size flow has TWO GP phases:
#   1. Mixed-GP           — macros movable, ends at "After Mixed-GP, best solution eval"
#   2. Macro Legalization — LP/cbc solve placing the macros, then `Reset optimizer` and a
#                           SECOND full GP pass with the macros held fixed
# The familiar "GP Stop!" masked_hpwl line is the end of phase 2. This table is the PHASE-1
# (Mixed-GP) reference. sw_only's own two-phase run (TODO #13, landed 2026-08-01) is the
# comparable *result*, but that table's numbers below are still phase-1-only.
# Two consequences that have burned us before:
#   - Mixed-GP ends at 0.10-0.18 exact overflow on nearly every MMS design (newblue3 is the
#     lone outlier at 0.040). It does NOT reach the 0.07 stop threshold. Us not reaching it
#     with macros movable is not by itself a defect.
#   - This overflow is BOTH filler-EXCLUDED and macro-EXCLUDED, not "+fillers" as an earlier
#     version of this comment claimed. XPlace's evaluate_placement() call at this checkpoint
#     (run_placement_nesterov.py:173/180-181) runs under ps.zero_macro_grad=True (drops
#     is_mov_macro nodes, evaluator.py:26-45) and reassembles node_pos as
#     mov_node_pos[mov_lhs:mov_rhs] + data.node_pos[mov_rhs:] -- the filler positions appended
#     past mov_rhs by get_mov_node_info() are sliced off. Compare against sw_only's
#     "macro-excluded" number (Placer::computeOverflow(clamp=false, include_fillers=false,
#     exclude_macros=true); logged as [OVFW-DIAG] macro-excluded=, or the "Macro-Excluded
#     Overflow (exact, no fillers)" summary row on mixed-size runs), NOT "Final Overflow
#     (exact, +fillers)" -- that one is both filler- and macro-INCLUDED.
#
# Source: local XPlace runs 2026-07-17, `--dataset mms --mixed_size True --seed 42`,
# ~/phd/Xplace/result/<ts>_<design>/log/test.log. Same seed as our sweeps.
_XPLACE_MMS_MIXED_GP = {
    "adaptec1": (6.238490e7, 0.1306), "adaptec2": (7.128972e7, 0.0963),
    "adaptec3": (1.534520e8, 0.1247), "adaptec4": (1.363127e8, 0.1352),
    "adaptec5": (3.034692e8, 0.1485), "bigblue1": (8.295205e7, 0.1741),
    "bigblue2": (1.212054e8, 0.1052), "bigblue3": (2.705605e8, 0.1235),
    "bigblue4": (6.241426e8, 0.1295), "newblue1": (5.946208e7, 0.1361),
    "newblue2": (1.516131e8, 0.1426), "newblue3": (2.828155e8, 0.0400),
    "newblue4": (2.237576e8, 0.1818), "newblue5": (3.791967e8, 0.1697),
    "newblue6": (4.028956e8, 0.1419), "newblue7": (8.638253e8, 0.1517),
}

# XPlace MMS reference, PHASE 2 AND BEYOND -- the end of the flow whose phase 1 is the table
# above. Same 16 local runs, same logs, same seed: these numbers were always in those files;
# NEW_REPORT_phase2_mms_suite_20260802.md's "no local XPlace phase-2 reference exists, getting
# one needs 15 more XPlace runs" is WRONG and this table is the correction.
#
# Per design: (post_gp_hpwl, post_gp_overflow, post_lg_hpwl, post_dp_hpwl), from the log lines
#   "After GP, best solution eval, exact HPWL: ... exact Overflow: ..."   <- end of phase 2
#   "***** Finish Legalization, HPWL: ... *****"                          <- greedy+abacus LG
#   "After DP, HPWL: ..."                                                 <- the headline number
# These HPWLs are UNMASKED -- all nets. XPlace has two different HPWL functions and only one of
# them masks: fast_evaluator() uses masked_scale_hpwl(..., data.net_mask, ...) and produces the
# per-iteration "masked_hpwl:" lines including the one in "GP Stop!", whereas get_obj_hpwl() ->
# get_hpwl() calls hpwl_cuda.hpwl() with NO mask and produces every number in this table.
# So compare against sw_only's "Final HPWL (exact, all nets)" (final_hpwl_exact, Output.cpp:511,
# threshold 1e9), NOT "Final HPWL" (final_hpwl, masked at ignore_net_degree = 100). On MMS
# adaptec1 the two differ by 0.06% -- small, but it is 2 nets of 221142 and it is free to get right.
#
# Use post_dp_hpwl as the quality metric, NOT post_gp_hpwl: legalization costs 1-8% HPWL and an
# under-spread GP pays more of it, so GP-vs-GP flatters whichever placer spread less (TODO #3).
# post_gp_overflow here is XPlace's EXACT overflow and is NOT the "GP Stop!" number -- that line
# prints the *masked* (smoothed) overflow, typically ~0.045 where the exact value is ~0.11-0.18.
# Comparing our exact overflow against that masked figure makes us look 2-3x worse than we are.
_XPLACE_MMS_FINAL = {
    "adaptec1": (6.457148e7, 0.1146, 7.012652e7, 6.813568e7),
    "adaptec2": (7.269954e7, 0.1314, 7.773626e7, 7.617702e7),
    "adaptec3": (1.544092e8, 0.1232, 1.613617e8, 1.590915e8),
    "adaptec4": (1.369644e8, 0.1172, 1.436678e8, 1.413627e8),
    "adaptec5": (3.097642e8, 0.1504, 3.146759e8, 3.130691e8),
    "bigblue1": (8.333430e7, 0.1178, 8.651378e7, 8.567288e7),
    "bigblue2": (1.216634e8, 0.1105, 1.269926e8, 1.256618e8),
    "bigblue3": (2.627814e8, 0.1061, 2.830117e8, 2.767256e8),
    "bigblue4": (6.271247e8, 0.1134, 6.535862e8, 6.464361e8),
    "newblue1": (5.836766e7, 0.1264, 6.089079e7, 6.004748e7),
    "newblue2": (1.486573e8, 0.1113, 1.538110e8, 1.523920e8),
    "newblue3": (2.691846e8, 0.1180, 2.736333e8, 2.726504e8),
    "newblue4": (2.299293e8, 0.1840, 2.322094e8, 2.298460e8),
    "newblue5": (3.845861e8, 0.1504, 3.922818e8, 3.898915e8),
    "newblue6": (4.032442e8, 0.1078, 4.107340e8, 4.083356e8),
    "newblue7": (8.657061e8, 0.1283, 8.855537e8, 8.803187e8),
}

# XPlace end-of-flow reference for the two NON-mixed-size tiers, keyed "<suite>/<design>" because
# adaptec1-4 / bigblue1-4 exist in BOTH ispd2005 and mms and are different designs.
# Per design: (post_gp_hpwl_exact, post_gp_overflow_exact, post_lg_hpwl, post_dp_hpwl) -- the same
# four lines and the same unmasked/exact conventions as _XPLACE_MMS_FINAL above.
#
# Captured 2026-08-07, `--seed 42 --num_threads 8 --load_from_raw True`, raw data in
# .claude/2_ARTIFACTS/xplace_ref_ispd.tsv, runners .claude/2_ARTIFACTS/run_xplace_ref{,_2015}.sh.
#
# ⚠️ TIER 2 IS IN SITE UNITS. These ispd2015 HPWLs are XPlace's own, i.e. raw DBU / site_width
# (database.py:602, `hpwl_scale = die_scale / site_width`). Multiply by SITE_WIDTH[path] before
# comparing to a sw_only HPWL. site_width is NOT uniform: 200 for the 15 mgc_* designs, 100 for
# the 5 mgc_superblue* -- a blanket x200 is wrong by 2x on those five.
#
# ispd2015 is now 20 of 20, from TWO DATA VARIANTS, and the split is not cosmetic (TODO #26):
#   11 designs, no fence regions -- run on our raw files via `--custom_path`, which bypasses the
#       `--dataset ispd2015` -> `ispd2015_fix` rewrite at Xplace/main.py:94-96.
#    9 designs WITH REGIONS/GROUPS -- run on `ispd2015_fix`, XPlace's own fence-stripped variant,
#       regenerated 2026-08-11 by `cd ~/phd/Xplace/data && python3 fix_ispd2015_route.py`. XPlace
#       raises NotImplementedError in compute_filler_with_fence on the raw data, so this variant is
#       the only one on which it produces a number at all -- and it is the variant the TCAD paper's
#       dagger-marked designs use (Table III/V, footnote 2). Regeneration was validated against the
#       2026-07-13 copy of mgc_pci_bridge32_b: the DEF is BYTE-IDENTICAL.
# The 9 are therefore fence-stripped on BOTH sides of the ratio (sw_only discards REGIONS/GROUPS at
# parse time regardless), which is what makes them comparable. It does NOT make either placement
# fence-legal: ours puts 59-94% of the constrained cells outside their region. See
# tools/fence_check.py and TODO #26.
_XPLACE_ISPD_FINAL = {
    # --- ispd2005 (8 of 8) ---
    "ispd2005/adaptec1": (7.068107e+07, 0.1099, 7.421337e+07, 7.310315e+07),
    "ispd2005/adaptec2": (7.861516e+07, 0.1269, 8.236126e+07, 8.131825e+07),
    "ispd2005/adaptec3": (1.862965e+08, 0.1290, 1.998780e+08, 1.938490e+08),
    "ispd2005/adaptec4": (1.677961e+08, 0.1150, 1.769727e+08, 1.733490e+08),
    "ispd2005/bigblue1": (8.725546e+07, 0.1194, 8.967986e+07, 8.908078e+07),
    "ispd2005/bigblue2": (1.309736e+08, 0.1112, 1.394881e+08, 1.369654e+08),
    "ispd2005/bigblue3": (2.910774e+08, 0.1125, 3.090403e+08, 3.029441e+08),
    "ispd2005/bigblue4": (7.256538e+08, 0.1126, 7.482465e+08, 7.425524e+08),
    # --- ispd2015, no fence regions: raw data via --custom_path (11) ---
    "ispd2015/mgc_des_perf_1":     (5.453202e+06, 0.1167, 5.842434e+06, 5.632567e+06),
    "ispd2015/mgc_fft_1":          (1.917159e+06, 0.1325, 2.059146e+06, 2.024385e+06),
    "ispd2015/mgc_fft_2":          (1.707055e+06, 0.1565, 1.843155e+06, 1.810411e+06),
    "ispd2015/mgc_fft_a":          (2.968405e+06, 0.1670, 3.092991e+06, 3.061765e+06),
    "ispd2015/mgc_fft_b":          (4.034900e+06, 0.1849, 4.219101e+06, 4.181930e+06),
    "ispd2015/mgc_matrix_mult_1":  (1.001578e+07, 0.1780, 1.068492e+07, 1.050204e+07),
    "ispd2015/mgc_matrix_mult_2":  (1.020299e+07, 0.1782, 1.088948e+07, 1.070034e+07),
    "ispd2015/mgc_matrix_mult_a":  (1.466008e+07, 0.1742, 1.538942e+07, 1.516973e+07),
    "ispd2015/mgc_superblue12":    (2.527073e+08, 0.1436, 2.590678e+08, 2.571014e+08),
    "ispd2015/mgc_superblue14":    (2.263669e+08, 0.1300, 2.306337e+08, 2.282981e+08),
    "ispd2015/mgc_superblue19":    (1.544428e+08, 0.1323, 1.573560e+08, 1.555965e+08),
    # --- ispd2015, FENCE REGIONS STRIPPED: `--dataset ispd2015_fix` (9; the paper's dagger set) ---
    # Captured 2026-08-11 apart from mgc_pci_bridge32_b, which is the 2026-08-07 run on the
    # byte-identical earlier copy of the same data. Cross-checked against TCAD Table III (which is
    # in microns: multiply by SITE_WIDTH, divide by the DEF's 1000 DBU/micron) -- agreement is
    # within 1% on 5 of the 9 and never worse than 5.4%.
    "ispd2015/mgc_des_perf_a":     (9.819075e+06, 0.1460, 1.003364e+07, 9.897489e+06),
    "ispd2015/mgc_des_perf_b":     (7.769324e+06, 0.1447, 7.966295e+06, 7.865749e+06),
    "ispd2015/mgc_edit_dist_a":    (2.065261e+07, 0.1498, 2.098810e+07, 2.079566e+07),
    "ispd2015/mgc_matrix_mult_b":  (1.337186e+07, 0.1702, 1.402243e+07, 1.383469e+07),
    "ispd2015/mgc_matrix_mult_c":  (1.292231e+07, 0.1672, 1.358381e+07, 1.338843e+07),
    "ispd2015/mgc_pci_bridge32_a": (1.647415e+06, 0.2169, 1.738537e+06, 1.707594e+06),
    "ispd2015/mgc_pci_bridge32_b": (3.432114e+06, 0.2431, 3.495595e+06, 3.477053e+06),
    "ispd2015/mgc_superblue11_a":  (3.316791e+08, 0.1374, 3.402111e+08, 3.354093e+08),
    "ispd2015/mgc_superblue16_a":  (2.534746e+08, 0.1412, 2.584717e+08, 2.554021e+08),
}

# LEF site width in DBU: SITE SIZE x (microns) * UNITS DATABASE MICRONS, read from each design's
# tech.lef. Only tier 2 needs it -- it is the factor between XPlace's site-unit HPWL and our raw
# DBU. Tier 1 and 3 are bookshelf and already share sw_only's frame (site_width 100 cancels the
# read-scale), so their entries are 1 and applying the conversion is a no-op.
# **Not uniform within tier 2** -- mgc_superblue* are 100, everything else 200.
_SITE_WIDTH_100 = ("mgc_superblue11_a", "mgc_superblue12", "mgc_superblue14",
                   "mgc_superblue16_a", "mgc_superblue19")
SITE_WIDTH = {f"{suite}/{name}": (100 if name in _SITE_WIDTH_100 else 200)
              for (name, suite, _t, _g, _d) in _ROWS if suite == "ispd2015"}


def _xplace_ref(suite, name):
    """(post_gp_hpwl, post_gp_overflow, post_lg_hpwl, post_dp_hpwl) or a 4-tuple of None."""
    if suite == "mms":
        return _XPLACE_MMS_FINAL[name]
    return _XPLACE_ISPD_FINAL.get(f"{suite}/{name}", (None, None, None, None))


def xplace_hpwl_in_sw_frame(path, hpwl):
    """Convert an XPlace HPWL for `path` into sw_only's raw-DBU frame.

    Tier 2 only: XPlace divides by site_width (database.py:602), so its mgc_* numbers are in site
    units and must be multiplied back. A no-op elsewhere. Always route a tier-2 comparison through
    this rather than hardcoding 200 -- the superblue designs are 100.
    """
    return None if hpwl is None else hpwl * SITE_WIDTH.get(path, 1)


# XPlace GP-only MASKED HPWL -- the "GP Stop! ... masked_hpwl:" line, nets > ignore_net_degree
# dropped. This is the reference that pairs with sw_only's masked "Best GP HPWL" (both mask), and
# it is NOT the exact post-GP number in _XPLACE_ISPD_FINAL[0] -- on mgc_superblue12 the two differ
# 7.9% (masked 2.342e8 vs exact 2.527e8), so pairing exact-with-masked would inject that error.
# ispd2005 values are raw DBU (bookshelf frame == ours); ispd2015 values are XPlace's SITE-UNIT
# frame and must go through xplace_hpwl_in_sw_frame(). Sources: ispd2005 + mgc_pci_bridge32_b from
# the 2026-08-07 local GP logs; the other 19 ispd2015 from .claude/2_ARTIFACTS/xplace_ref_ispd.tsv
# (gp_masked_hpwl). MMS has no masked-GP reference (its two-phase flow is a different comparison).
_XPLACE_GP_MASKED = {
    "ispd2005/adaptec1": 7.063986e7, "ispd2005/adaptec2": 7.851941e7,
    "ispd2005/adaptec3": 1.862087e8, "ispd2005/adaptec4": 1.676566e8,
    "ispd2005/bigblue1": 8.721272e7, "ispd2005/bigblue2": 1.298882e8,
    "ispd2005/bigblue3": 2.909645e8, "ispd2005/bigblue4": 7.231753e8,
    "ispd2015/mgc_des_perf_1": 5.448811e6, "ispd2015/mgc_des_perf_a": 9.810134e6,
    "ispd2015/mgc_des_perf_b": 7.763363e6, "ispd2015/mgc_edit_dist_a": 2.064468e7,
    "ispd2015/mgc_fft_1": 1.914535e6, "ispd2015/mgc_fft_2": 1.703669e6,
    "ispd2015/mgc_fft_a": 2.960441e6, "ispd2015/mgc_fft_b": 4.026938e6,
    "ispd2015/mgc_matrix_mult_1": 1.001032e7, "ispd2015/mgc_matrix_mult_2": 1.019749e7,
    "ispd2015/mgc_matrix_mult_a": 1.464677e7, "ispd2015/mgc_matrix_mult_b": 1.335916e7,
    "ispd2015/mgc_matrix_mult_c": 1.290962e7, "ispd2015/mgc_pci_bridge32_a": 1.643455e6,
    "ispd2015/mgc_pci_bridge32_b": 3.424154e6, "ispd2015/mgc_superblue11_a": 3.249247e8,
    "ispd2015/mgc_superblue12": 2.342350e8, "ispd2015/mgc_superblue14": 2.214621e8,
    "ispd2015/mgc_superblue16_a": 2.463811e8, "ispd2015/mgc_superblue19": 1.493887e8,
}


def xplace_gp_masked_in_sw_frame(path):
    """XPlace's masked GP-stop HPWL for `path`, converted into sw_only's frame; None if unknown.
    Pairs with sw_only's masked 'Best GP HPWL'."""
    return xplace_hpwl_in_sw_frame(path, _XPLACE_GP_MASKED.get(path))


# Canonical path is "suite/design" (matches host/benchmarks/<suite>/<design> and
# the dse.py benchmark-override format).
BENCHMARKS = {
    f"{suite}/{name}": dict(name=name, suite=suite, tier=tier,
                            grid=grid, target_density=dens,
                            site_width=SITE_WIDTH.get(f"{suite}/{name}", 1),
                            # phase-comparable XPlace reference; MMS only (the other tiers have
                            # one GP phase, so their post-GP number IS the end-of-flow one below)
                            xplace_gp_hpwl=_XPLACE_MMS_MIXED_GP[name][0] if suite == "mms" else None,
                            xplace_gp_overflow=_XPLACE_MMS_MIXED_GP[name][1] if suite == "mms" else None,
                            # end-of-flow reference: (phase 2,) then legalization, then DP.
                            # None on the 8 ispd2015 designs with no reference (TODO #22).
                            xplace_final_hpwl=_xplace_ref(suite, name)[0],
                            xplace_final_overflow=_xplace_ref(suite, name)[1],
                            xplace_lg_hpwl=_xplace_ref(suite, name)[2],
                            xplace_dp_hpwl=_xplace_ref(suite, name)[3])
    for (name, suite, tier, grid, dens) in _ROWS
}

# Fast, XPlace-referenced screening picks for sensitivity work.
SCREENING_DEFAULT = "ispd2015/mgc_fft_a"   # ~31K nodes, ~60s/run
SCREENING_ANCHOR  = "ispd2005/adaptec1"    # Tier 1, clean XPlace ratio (slow)
SCREENING_STALL   = "ispd2015/mgc_des_perf_1"  # convergence-control stress


def all_paths():
    return list(BENCHMARKS.keys())


def is_valid(path):
    return path in BENCHMARKS


def resolve(name_or_path):
    """Return the canonical 'suite/design' path, or raise SystemExit with the
    valid list. Accepts a full 'suite/design' path, or a bare design name when
    it is unambiguous (some names — adaptec1 — exist in both ispd2005 and mms,
    so those must be given with the suite prefix)."""
    if name_or_path in BENCHMARKS:
        return name_or_path
    # bare design name?
    hits = [p for p, m in BENCHMARKS.items() if m["name"] == name_or_path]
    if len(hits) == 1:
        return hits[0]
    if len(hits) > 1:
        raise SystemExit(f"benchmark '{name_or_path}' is ambiguous across suites "
                         f"{sorted(BENCHMARKS[h]['suite'] for h in hits)}; "
                         f"give it as 'suite/design' (e.g. {hits[0]}).")
    raise SystemExit(f"benchmark '{name_or_path}' is NOT in the master manifest "
                     f"(tools/benchmarks.py). Valid designs:\n  " +
                     "\n  ".join(all_paths()))


def by_tier(tier):
    return [p for p, m in BENCHMARKS.items() if m["tier"] == tier]


# Whole-tier tokens for a design spec, by tier number or suite name (case-insensitive).
_GROUPS = {"tier1": 1, "ispd2005": 1, "tier2": 2, "ispd2015": 2, "tier3": 3, "mms": 3}


def expand_designs(spec):
    """A design spec -> ['suite/design', ...]. Accepts "all"; a whole tier by number or suite name
    (tier1/ispd2005, tier2/ispd2015, tier3/mms); "+"-joined groups (tier1+tier2, ispd2005+mms); or
    comma-separated design names / "suite/design" paths. Typos raise via resolve()."""
    paths = []
    for token in spec.replace("+", ",").split(","):
        token = token.strip()
        if not token:
            continue
        if token.lower() == "all":
            paths += all_paths()
        elif token.lower() in _GROUPS:
            paths += by_tier(_GROUPS[token.lower()])
        else:
            paths.append(resolve(token))          # case-sensitive design names
    return list(dict.fromkeys(paths))             # dedupe, order preserved


def to_markdown():
    lines = [
        "# Benchmark manifest",
        "",
        "Auto-generated from `tools/benchmarks.py` (`python3 tools/benchmarks.py --write-md`).",
        "The master list of designs we work on: present in the XPlace dataset, tuned in "
        "XPlace's `setup_dataset.py`, and present locally under `host/benchmarks/`. "
        "Grid / target-density are XPlace's own per-design tuned values.",
        "",
    ]
    tier_desc = {
        1: "Tier 1 — ISPD2005 (fixed macros; frame matches sw_only → honest XPlace ratio; the anchor set)",
        2: "Tier 2 — ISPD2015 mgc_* (site-width frame; XPlace HPWL needs ×site_width conversion)",
        3: "Tier 3 — MMS (mixed-size / movable macros; separate regime, preconditioner matters)",
    }
    for tier in (1, 2, 3):
        rows = [BENCHMARKS[p] for p in by_tier(tier)]
        ref = tier == 3   # only the MMS tier carries a phase-comparable XPlace reference
        head = "| design | suite | XPlace grid | target density |"
        if ref:
            head += (" XPlace Mixed-GP HPWL | XPlace Mixed-GP overflow |"
                     " XPlace post-GP HPWL | XPlace post-LG HPWL | XPlace post-DP HPWL |")
        lines += [f"## {tier_desc[tier]}", ""]
        if ref:
            lines += ["Two XPlace reference points, both from the same 2026-07-17 local runs:",
                      "",
                      "- **Mixed-GP** = phase 1, macros movable — NOT the `GP Stop!` line. Its "
                      "overflow EXCLUDES both fillers and movable macros (XPlace's "
                      "zero_macro_grad at this checkpoint); compare against sw_only's "
                      "macro-excluded overflow, not \"Final Overflow (exact, +fillers)\". "
                      "See `_XPLACE_MMS_MIXED_GP`.",
                      "- **post-GP / post-LG / post-DP** = the end of the flow (phase 2, then "
                      "legalization, then detailed placement). **post-DP HPWL is the headline "
                      "quality metric** — legalization costs 1-8% HPWL and an under-spread GP "
                      "pays more of it, so a GP-vs-GP comparison flatters whichever placer "
                      "spread less. See `_XPLACE_MMS_FINAL`.",
                      "",
                      "Both HPWL columns are **unmasked (all nets)** — XPlace's `get_obj_hpwl` "
                      "calls `hpwl_cuda.hpwl` with no `net_mask`; only the per-iteration "
                      "`masked_hpwl:` line (including the one inside `GP Stop!`) is masked. "
                      "Compare against sw_only's \"Final HPWL (exact, all nets)\", not "
                      "\"Final HPWL\".",
                      ""]
        lines += [head, "|---|---|---|---|" + ("---|---|---|---|---|" if ref else "")]
        for m in rows:
            row = f"| {m['name']} | {m['suite']} | {m['grid']} | {m['target_density']:g} |"
            if ref:
                row += (f" {m['xplace_gp_hpwl']:.3e} | {m['xplace_gp_overflow']:.4f} |"
                        f" {m['xplace_final_hpwl']:.3e} | {m['xplace_lg_hpwl']:.3e} |"
                        f" {m['xplace_dp_hpwl']:.3e} |")
            lines.append(row)
        lines.append("")
    lines += [
        f"**Tier 1 + Tier 2 = {len(by_tier(1)) + len(by_tier(2))} designs = the XPlace-paper suite "
        f"(= dse.py `_full_suite`).**  Tier 3 = {len(by_tier(3))} mixed-size designs.",
        "",
        "**Out of scope** (do not add): ISPD2019 (local only, no XPlace data); classic "
        "superblue1–18 (XPlace-tuned but no data present).",
        "",
        "## Screening picks (sensitivity analysis)",
        f"- `{SCREENING_DEFAULT}` — fast default (~60s/run)",
        f"- `{SCREENING_ANCHOR}` — Tier 1 clean XPlace ratio anchor (slow, ~8–15 min/run)",
        f"- `{SCREENING_STALL}` — convergence-control stress (stalling std-cell design)",
        "",
    ]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    import sys
    if "--write-md" in sys.argv:
        import os
        # write BENCHMARKS.md at the repo (vck5000) root, i.e. one level up from tools/
        out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           "BENCHMARKS.md")
        with open(out, "w") as f:
            f.write(to_markdown())
        print(f"wrote {out}  ({len(BENCHMARKS)} designs)")
    else:
        print(f"{len(BENCHMARKS)} designs in manifest "
              f"(tier1={len(by_tier(1))}, tier2={len(by_tier(2))}, tier3={len(by_tier(3))})")
        for p in all_paths():
            print(" ", p)
