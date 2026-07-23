# benchmarks.py
# ----------------------------------------------------------------------------
# MASTER BENCHMARK MANIFEST — the single source of truth for which designs we
# work on. A design is on this list iff it is (a) physically present in the
# XPlace dataset (~/phd/Xplace/data/raw), (b) has a tuned config in XPlace's
# setup_dataset.py, and (c) present locally under host/benchmarks/. Efforts are
# limited to this list so every sw_only result has a comparable XPlace reference.
#
# Grid / target_density columns are XPlace's own per-design tuned values
# (Xplace/utils/setup_dataset.py::setup_design_args) — use them so a sweep runs
# each design at the same resolution XPlace did.
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

# (design_name, suite, tier, xplace_grid, xplace_target_density)
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
    ("mgc_superblue12",    "ispd2015", 2, 1024, 0.650),
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

# Canonical path is "suite/design" (matches host/benchmarks/<suite>/<design> and
# the dse.py benchmark-override format).
BENCHMARKS = {
    f"{suite}/{name}": dict(name=name, suite=suite, tier=tier,
                            grid=grid, target_density=dens)
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
        lines += [f"## {tier_desc[tier]}", "",
                  "| design | suite | XPlace grid | target density |",
                  "|---|---|---|---|"]
        for m in rows:
            lines.append(f"| {m['name']} | {m['suite']} | {m['grid']} | {m['target_density']:g} |")
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
