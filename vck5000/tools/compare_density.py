#!/usr/bin/env python3
"""Compare sw_only vs XPlace bin-density maps (physical / exact real-cell density).

sw_only dump  (Placer::dumpBinDensity): <bench>_density_rho_exact.csv / _rho_smoothed.csv
    plain CSV, one line per grid row y (0 = bottom), comma-separated over columns x.
    Values are rho = deposited_area / bin_area (fillers excluded).
XPlace dump  (run_placement_nesterov.py, XPLACE_DUMP_DENSITY=1):
    <bench>_density_exact.npy  shape [num_bin_x, num_bin_y], same rho convention,
    plus <bench>_density_meta.json.

Both maps are normalized to utilization u = rho / target_density (u > 1 == overflow),
block-mean downsampled to a common grid, and plotted side-by-side with a difference map
and marginal profiles. Usage:
    python compare_density.py SW_ONLY_EXACT_CSV XPLACE_NPY [--meta META.json] [--out OUT.png]
"""
import argparse, json, os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load_sw_only(csv_path):
    a = np.loadtxt(csv_path, delimiter=",")  # [y][x]
    return a


def load_xplace(npy_path, target_density):
    a = np.load(npy_path)          # [x][y]
    return a.T                     # -> [y][x]


def block_mean(a, ny, nx):
    """Area-conserving downsample of a [Y,X] map to [ny,nx] by block averaging."""
    Y, X = a.shape
    if (Y, X) == (ny, nx):
        return a.copy()
    ys = np.linspace(0, Y, ny + 1).astype(int)
    xs = np.linspace(0, X, nx + 1).astype(int)
    out = np.zeros((ny, nx))
    for j in range(ny):
        for i in range(nx):
            blk = a[ys[j]:ys[j + 1], xs[i]:xs[i + 1]]
            out[j, i] = blk.mean() if blk.size else 0.0
    return out


def stats(u, cap=1.0):
    return {
        "max_util": float(u.max()),
        "mean_util": float(u.mean()),
        "std_util": float(u.std()),
        "frac_bins_over_cap": float((u > cap).mean()),
        "overflow_mass": float(np.clip(u - cap, 0, None).sum() / u.sum()),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sw_only_exact_csv")
    ap.add_argument("xplace_npy")
    ap.add_argument("--meta", default=None)
    ap.add_argument("--out", default="density_compare.png")
    ap.add_argument("--grid", type=int, default=64, help="common resolution to compare at")
    args = ap.parse_args()

    meta = {}
    if args.meta and os.path.exists(args.meta):
        meta = json.load(open(args.meta))
    td = float(meta.get("target_density", 1.0))

    m = load_sw_only(args.sw_only_exact_csv) / td
    x = load_xplace(args.xplace_npy, td) / td

    g = args.grid
    mg = block_mean(m, g, g)
    xg = block_mean(x, g, g)
    diff = mg - xg

    sm, sx = stats(mg), stats(xg)
    print(f"sw_only native {m.shape}  XPlace native {x.shape}  compared at {g}x{g}")
    print(f"{'metric':<20}{'sw_only':>12}{'XPlace':>12}{'m - x':>12}")
    for k in sm:
        print(f"{k:<20}{sm[k]:>12.4f}{sx[k]:>12.4f}{sm[k]-sx[k]:>12.4f}")

    vmax = max(mg.max(), xg.max())
    fig, ax = plt.subplots(2, 3, figsize=(16, 10))
    for a, title, dat in [(ax[0, 0], "sw_only exact util", mg),
                          (ax[0, 1], "XPlace exact util", xg)]:
        im = a.imshow(dat, origin="lower", cmap="turbo", vmin=0, vmax=vmax)
        a.set_title(title); fig.colorbar(im, ax=a, fraction=0.046)
    d = np.abs(diff).max()
    im = ax[0, 2].imshow(diff, origin="lower", cmap="RdBu_r", vmin=-d, vmax=d)
    ax[0, 2].set_title("sw_only - XPlace (blue=XPlace denser)")
    fig.colorbar(im, ax=ax[0, 2], fraction=0.046)

    # capacity-exceedance masks
    ax[1, 0].imshow((mg > 1).astype(float), origin="lower", cmap="Reds", vmin=0, vmax=1)
    ax[1, 0].set_title(f"sw_only over-cap bins ({sm['frac_bins_over_cap']*100:.1f}%)")
    ax[1, 1].imshow((xg > 1).astype(float), origin="lower", cmap="Reds", vmin=0, vmax=1)
    ax[1, 1].set_title(f"XPlace over-cap bins ({sx['frac_bins_over_cap']*100:.1f}%)")

    # marginal (column-summed) utilization profiles
    ax[1, 2].plot(mg.sum(axis=0), label="sw_only", lw=2)
    ax[1, 2].plot(xg.sum(axis=0), label="XPlace", lw=2)
    ax[1, 2].set_title("column-summed utilization (x profile)")
    ax[1, 2].legend(); ax[1, 2].grid(alpha=0.3)

    fig.suptitle(f"Physical (exact) density: sw_only vs XPlace @ {g}x{g}", fontsize=14)
    fig.tight_layout()
    fig.savefig(args.out, dpi=110)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
