# analyze_sobol.py
# ----------------------------------------------------------------------------
# Sobol variance-based sensitivity ANALYSIS.
#   python3 tools/analyze_sobol.py <results/DSE_...dir> <results/sobol_...dir>
#
# Joins the DSE dse_results.csv (keyed by the "run" label) to the Saltelli sample,
# then per objective computes Sobol indices with SALib:
#   S1 : first-order  — variance explained by the factor ALONE
#   ST : total-order  — variance explained by the factor + ALL its interactions
#   ST - S1           — the interaction contribution (what Morris's sigma only hinted at)
#
# Divergent runs (NaN objective) are IMPUTED with the worst observed value — a
# hard-diverged config IS a bad outcome, and Saltelli's estimator needs the full
# structured Y vector (it cannot drop rows the way Morris drops trajectories).
# ----------------------------------------------------------------------------

import csv
import json
import os
import sys

import dse  # owns the results table + its name (dse.RESULTS_CSV)

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from SALib.analyze import sobol as sobol_analyze

OBJECTIVES = [
    ("Best GP HPWL",        "hpwl_gp"),
    ("Final HPWL Exact",    "hpwl_exact"),
    ("Best OVFW",           "overflow"),
    ("Iters",               "iters"),
    ("Total Runtime (sec)", "runtime"),
]


def read_results(dse_dir):
    rows = list(csv.DictReader(open(os.path.join(dse_dir, dse.RESULTS_CSV), newline="")))
    return {(r.get("run") or "").strip(): r for r in rows if (r.get("run") or "").strip()}


def to_float(v):
    try:
        if v is None or str(v).strip() in ("", "N/A", "nan"):
            return float("nan")
        return float(v)
    except (TypeError, ValueError):
        return float("nan")


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: analyze_sobol.py <DSE_dir> <sobol_dir>")
    dse_dir, sobol_dir = sys.argv[1], sys.argv[2]

    X = np.load(os.path.join(sobol_dir, "X.npy"))
    problem = json.load(open(os.path.join(sobol_dir, "problem.json")))
    meta = json.load(open(os.path.join(sobol_dir, "meta.json")))
    names = problem["names"]
    so = bool(meta.get("calc_second_order", False))
    n = X.shape[0]

    by_label = read_results(dse_dir)
    print(f"Loaded X {X.shape}, {len(by_label)} result rows; second_order={so}")

    out_dir = os.path.join(sobol_dir, "analysis")
    os.makedirs(out_dir, exist_ok=True)
    md = [f"# Sobol analysis — {meta.get('benchmark')} @ {meta.get('grid')}",
          f"\nN={meta.get('base_samples')}, {meta.get('num_vars')} factors, "
          f"{n} runs, second_order={so}\n"]

    for col, key in OBJECTIVES:
        # Build Y in X-row order (label m{i:05d}); impute NaN with the worst (max) value.
        Y = np.full(n, np.nan)
        missing = 0
        for i in range(n):
            r = by_label.get(f"m{i:05d}")
            if r is None:
                missing += 1
            else:
                Y[i] = to_float(r.get(col))
        n_nan = int(np.isnan(Y).sum())
        finite = Y[np.isfinite(Y)]
        if missing or finite.size < n * 0.5:
            print(f"[{key}] SKIP — {missing} missing rows / too few finite ({finite.size}/{n}).")
            md.append(f"## {col} — SKIPPED ({missing} missing, {finite.size} finite)\n")
            continue
        worst = finite.max()  # all objectives: higher = worse
        Y = np.where(np.isfinite(Y), Y, worst)
        if n_nan:
            print(f"[{key}] imputed {n_nan}/{n} divergent runs with worst value {worst:.3e}")

        Si = sobol_analyze.analyze(problem, Y, calc_second_order=so,
                                   num_resamples=100, conf_level=0.95, seed=1234)
        order = np.argsort(Si["ST"])[::-1]

        rows = [dict(factor=names[r], S1=Si["S1"][r], S1_conf=Si["S1_conf"][r],
                     ST=Si["ST"][r], ST_conf=Si["ST_conf"][r],
                     interaction=Si["ST"][r] - Si["S1"][r]) for r in order]
        with open(os.path.join(out_dir, f"sobol_{key}.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=["factor", "S1", "S1_conf", "ST", "ST_conf", "interaction"])
            w.writeheader(); w.writerows(rows)

        print(f"\n=== {col} ({key}) — ranked by ST ===")
        print(f"{'factor':32s} {'S1':>8s} {'ST':>8s} {'ST-S1':>8s}")
        md.append(f"## {col}\n\n| factor | S1 | ST | ST-S1 (interaction) |\n|---|---|---|---|")
        for rr in rows:
            print(f"{rr['factor']:32s} {rr['S1']:8.3f} {rr['ST']:8.3f} {rr['interaction']:8.3f}")
            md.append(f"| {rr['factor']} | {rr['S1']:.3f} | {rr['ST']:.3f} | {rr['interaction']:.3f} |")
        md.append(f"\nSum of S1 = {sum(r['S1'] for r in rows):.3f} "
                  f"(≈1 means additive/no interactions; «1 means strong interactions)\n")

        # S1 vs ST bar chart
        idx = np.arange(len(names))
        s1 = [Si["S1"][i] for i in range(len(names))]
        st = [Si["ST"][i] for i in range(len(names))]
        ordv = np.argsort(st)[::-1]
        fig, ax = plt.subplots(figsize=(10, 6))
        ax.bar(idx - 0.2, [st[i] for i in ordv], 0.4, label="ST (total)")
        ax.bar(idx + 0.2, [s1[i] for i in ordv], 0.4, label="S1 (first-order)")
        ax.set_xticks(idx); ax.set_xticklabels([names[i] for i in ordv], rotation=45, ha="right", fontsize=8)
        ax.set_ylabel("Sobol index"); ax.set_title(f"Sobol — {col} ({meta.get('benchmark')})")
        ax.legend(); fig.tight_layout()
        fig.savefig(os.path.join(out_dir, f"sobol_{key}.png"), dpi=120)
        plt.close(fig)

    open(os.path.join(out_dir, "sobol_summary.md"), "w").write("\n".join(md) + "\n")
    print(f"\nSummary: {os.path.join(out_dir, 'sobol_summary.md')}")


if __name__ == "__main__":
    main()
