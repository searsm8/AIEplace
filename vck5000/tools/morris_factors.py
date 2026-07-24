# morris_factors.py
# ----------------------------------------------------------------------------
# Factor definitions for the Morris (Elementary Effects) sensitivity screen of
# the AIEplace placement hyperparameters. This is the single editable source of
# truth: edit the ranges / add-remove factors here; morris.py (sampler) and
# analyze_morris.py (analysis) both import FACTORS from this file.
#
# Scope of this first pass (decided 2026-07-22): the continuous *tuning*
# hyperparameters + the *soft* algorithm on/off toggles. Held FIXED at their
# run_config.json defaults (NOT screened here):
#   - field-frame faithfulness flags (dct_normalize, dct_normalize_inverse,
#     precond_raw_area, dff_force_ratio, gamma_bin_scaled) — decided + mutually
#     interdependent, so varying them muddies the elementary effects.
#   - convergence / stopping criteria (convergence_overflow_threshold,
#     convergence_*_iterations, ...) — moving the stop point makes HPWL
#     non-comparable across runs; HPWL is the headline objective this pass.
#   - init_method (held random_center) so init_spread stays inactive.
#   - enable_backtracking (held True) so backtrack_epsilon / backtrack_max_tries
#     are ALWAYS active — avoids the conditional-inactive-factor confound that
#     would inflate their Morris sigma.
# ----------------------------------------------------------------------------

# Each factor:
#   name    : the run_config.json param key (also the dse.py override key)
#   section : config section for the dse.py override (all here are "params")
#   kind    : how the sampled unit value maps to a config value —
#               "float" : linear in [lo, hi]
#               "log"   : log10-uniform in [lo, hi]  (lo,hi are the REAL bounds;
#                         SALib samples log10 space, morris.py exponentiates)
#               "int"   : linear in [lo, hi], rounded to nearest integer
#               "bool"  : sampled in [0,1], thresholded at 0.5 -> False/True
#   bounds  : (lo, hi) in REAL units (log/int described above)
#   default : the run_config.json value, for reference / the ranges table

FACTORS = [
    # --- continuous tuning hyperparameters -------------------------------
    dict(name="init_gamma",                     section="params", kind="float", bounds=(1.0, 16.0),   default=4),
    dict(name="density_weight_init_multiplier", section="params", kind="log",   bounds=(8e-6, 8e-4),  default=8e-5),
    dict(name="density_weight_max_step",        section="params", kind="float", bounds=(1.02, 1.10),  default=1.05),
    dict(name="init_step_length",               section="params", kind="log",   bounds=(1e-3, 1.0),   default=0.1),
    dict(name="backtrack_epsilon",              section="params", kind="float", bounds=(1.01, 1.20),  default=1.05),
    dict(name="gamma_ref_grid",                 section="params", kind="int",   bounds=(256, 1024),   default=512),

    # --- soft algorithm on/off toggles (2-level) -------------------------
    dict(name="enable_momentum",                section="params", kind="bool",  bounds=(0, 1),        default=True),
    dict(name="enable_filler",                  section="params", kind="bool",  bounds=(0, 1),        default=True),
    dict(name="enable_density_clamp",           section="params", kind="bool",  bounds=(0, 1),        default=True),
    dict(name="gamma_schedule",                 section="params", kind="bool",  bounds=(0, 1),        default=True),
    dict(name="enable_preconditioning",         section="params", kind="bool",  bounds=(0, 1),        default=False),  # auto->OFF on fft_a (no movable macros)
]

# Params held fixed for THIS pass but forced explicitly into every run's config
# so the sweep is reproducible regardless of run_config.json's current values.
FIXED_OVERRIDES = dict(
    benchmark="ispd2005/adaptec1_PLACEHOLDER",  # overwritten by morris.py --benchmark
    init_method="random_center",
    enable_backtracking=True,
    random_seed=42,
    # Screened OUT after the r=9 fft_a pass (2026-07-22): bottom-6 on ALL four objectives
    # (HPWL/overflow/iters/runtime), never ranked better than 11th of 16. Pinned at defaults.
    # NOTE: adaptation_window / slow_improvement_threshold / high_overflow_threshold are the
    # emergency-jolt plateau-detection trio — inert here only because fft_a converges cleanly and
    # never triggers the jolt. RE-ADD them (move back into FACTORS) when screening a STALLING
    # design (e.g. mgc_des_perf_1), where the jolt mechanism actually fires. See [[nonconverge_root_cause]].
    density_weight_min_step=0.95,
    backtrack_max_tries=10,
    adaptation_window=25,
    slow_improvement_threshold=1e-3,
    high_overflow_threshold=0.7,
)


def salib_problem():
    """Build the SALib problem dict. Bounds are in the space SALib samples:
    log factors use log10(bounds); everything else uses real bounds. The
    unit->config mapping (exp/round/threshold) lives in map_sample_row()."""
    import math
    names, bounds = [], []
    for f in FACTORS:
        lo, hi = f["bounds"]
        if f["kind"] == "log":
            bounds.append([math.log10(lo), math.log10(hi)])
        else:
            bounds.append([float(lo), float(hi)])
        names.append(f["name"])
    return {"num_vars": len(FACTORS), "names": names, "bounds": bounds}


def map_sample_row(row):
    """Map one SALib sample row (list of floats, one per FACTOR) to a dict of
    run_config overrides in real config units."""
    out = {}
    for f, x in zip(FACTORS, row):
        k = f["kind"]
        if k == "log":
            out[f["name"]] = float(10.0 ** x)
        elif k == "int":
            out[f["name"]] = int(round(x))
        elif k == "bool":
            out[f["name"]] = bool(x >= 0.5)
        else:  # float
            out[f["name"]] = float(x)
    return out
