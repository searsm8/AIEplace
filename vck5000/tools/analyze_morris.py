# analyze_morris.py
# ----------------------------------------------------------------------------
# Morris (Elementary Effects) ANALYSIS for the AIEplace hyperparameter screen.
#
#   python3 tools/analyze_morris.py <results/DSE_...dir> <results/morris_...dir>
#
# Joins the DSE dse_results.csv (one row per Morris sample, keyed by the "run"
# label) back to the sampled X matrix, then for each objective computes the
# Morris indices with SALib:
#   mu*    : mean |elementary effect| — overall influence ranking (the headline)
#   sigma  : std of EEs — nonlinearity / interaction with other factors
#   mu     : mean signed EE — direction (sign) of influence
# Ranks factors by mu*, writes a CSV + markdown table, and a mu* vs sigma plot,
# per objective. HPWL (Best GP HPWL) is the headline objective.
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
from SALib.analyze import morris as morris_analyze

# objective column in dse_results.csv -> (short key, "min" is better? for reporting)
OBJECTIVES = [
    ("Best GP HPWL",        "hpwl_gp",   True),   # headline QoR (masked GP HPWL)
    ("Final HPWL Exact",    "hpwl_exact", True),
    ("Best OVFW",           "overflow",  True),
    ("Iters",               "iters",     True),
    ("Total Runtime (sec)", "runtime",   True),
]


def read_results(dse_dir):
    """label -> {column: value} from the DSE dse_results.csv (csv handles the
    quoted embedded newlines in the HYPERLINK cells)."""
    path = os.path.join(dse_dir, dse.RESULTS_CSV)
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    by_label = {}
    for r in rows:
        lbl = (r.get("run") or "").strip()
        if lbl:
            by_label[lbl] = r
    return by_label


def to_float(v):
    try:
        if v is None or str(v).strip() in ("", "N/A", "nan"):
            return float("nan")
        return float(v)
    except (TypeError, ValueError):
        return float("nan")


def build_Y(by_label, n, col):
    """Y in X-row order: X row i has label m{i:04d}."""
    Y = np.full(n, np.nan)
    missing = []
    for i in range(n):
        lbl = f"m{i:04d}"
        r = by_label.get(lbl)
        if r is None:
            missing.append(lbl)
            continue
        Y[i] = to_float(r.get(col))
    return Y, missing


def keep_complete_trajectories(X, Y, k):
    """Morris needs complete trajectories (blocks of k+1 rows). A run that
    failed / produced NaN breaks only its own trajectory, so drop that whole
    block and analyze on the survivors. Returns (X_ok, Y_ok, n_traj_dropped)."""
    block = k + 1
    n_traj = X.shape[0] // block
    keep = np.zeros(X.shape[0], dtype=bool)
    dropped = 0
    for t in range(n_traj):
        sl = slice(t * block, (t + 1) * block)
        if np.all(np.isfinite(Y[sl])):
            keep[sl] = True
        else:
            dropped += 1
    return X[keep], Y[keep], dropped


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: analyze_morris.py <DSE_dir> <morris_dir>")
    dse_dir, morris_dir = sys.argv[1], sys.argv[2]

    X = np.load(os.path.join(morris_dir, "X.npy"))
    problem = json.load(open(os.path.join(morris_dir, "problem.json")))
    meta = json.load(open(os.path.join(morris_dir, "meta.json")))
    names = problem["names"]
    num_levels = meta.get("num_levels", 4)
    n = X.shape[0]

    by_label = read_results(dse_dir)
    print(f"Loaded X {X.shape}, {len(by_label)} result rows from {dse_dir}")

    out_dir = os.path.join(morris_dir, "analysis")
    os.makedirs(out_dir, exist_ok=True)
    md = [f"# Morris screen — {meta.get('benchmark')} @ {meta.get('grid')}",
          f"\n{meta.get('trajectories')} trajectories x {meta.get('num_vars')} factors "
          f"= {n} runs, num_levels={num_levels}\n"]

    k = problem["num_vars"]
    for col, key, _min_better in OBJECTIVES:
        Y, missing = build_Y(by_label, n, col)
        Xok, Yok, dropped = keep_complete_trajectories(X, Y, k)
        n_traj_ok = Xok.shape[0] // (k + 1)
        if n_traj_ok < 2:
            print(f"[{key}] SKIP — only {n_traj_ok} complete trajectory(ies).")
            md.append(f"## {col} — SKIPPED (only {n_traj_ok} complete trajectories)\n")
            continue
        note = ""
        if dropped:
            note = (f"  [dropped {dropped} incomplete trajectory(ies) -> "
                    f"r={n_traj_ok}; missing runs e.g. {missing[:3]}]")
            print(f"[{key}]{note}")

        Si = morris_analyze.analyze(problem, Xok, Yok, num_levels=num_levels,
                                    num_resamples=1000, conf_level=0.95, seed=1234)
        order = np.argsort(Si["mu_star"])[::-1]

        # ranking table
        rows = []
        for r in order:
            rows.append(dict(factor=names[r], mu_star=Si["mu_star"][r],
                             mu_star_conf=Si["mu_star_conf"][r],
                             sigma=Si["sigma"][r], mu=Si["mu"][r]))
        # csv
        csv_path = os.path.join(out_dir, f"morris_{key}.csv")
        with open(csv_path, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=["factor", "mu_star", "mu_star_conf", "sigma", "mu"])
            w.writeheader()
            w.writerows(rows)

        print(f"\n=== {col} ({key}) — ranked by mu* ===")
        print(f"{'factor':32s} {'mu*':>11s} {'sigma':>11s} {'mu':>11s}")
        md.append(f"## {col}{(' ' + note.strip()) if dropped else ''}\n\n"
                  f"| factor | mu* | mu*_conf | sigma | mu (signed) |\n"
                  f"|---|---|---|---|---|")
        for rr in rows:
            print(f"{rr['factor']:32s} {rr['mu_star']:11.4g} {rr['sigma']:11.4g} {rr['mu']:11.4g}")
            md.append(f"| {rr['factor']} | {rr['mu_star']:.4g} | {rr['mu_star_conf']:.3g} "
                      f"| {rr['sigma']:.4g} | {rr['mu']:.4g} |")
        md.append("")

        # mu* vs sigma plot
        fig, ax = plt.subplots(figsize=(8, 6))
        ax.scatter(Si["mu_star"], Si["sigma"])
        for i, nm in enumerate(names):
            ax.annotate(nm, (Si["mu_star"][i], Si["sigma"][i]), fontsize=8,
                        xytext=(4, 2), textcoords="offset points")
        ax.set_xlabel("mu*  (overall influence)")
        ax.set_ylabel("sigma  (nonlinearity / interaction)")
        ax.set_title(f"Morris — {col}  ({meta.get('benchmark')})")
        fig.tight_layout()
        png = os.path.join(out_dir, f"morris_{key}.png")
        fig.savefig(png, dpi=120)
        plt.close(fig)
        print(f"  -> {csv_path}\n  -> {png}")

    with open(os.path.join(out_dir, "morris_summary.md"), "w") as f:
        f.write("\n".join(md) + "\n")
    print(f"\nSummary: {os.path.join(out_dir, 'morris_summary.md')}")


if __name__ == "__main__":
    main()
