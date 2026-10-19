#!/usr/bin/env python3
"""sum_order_drift.py -- #45 step 5b: what moving sw_flat_place's sums to slot order (and its exp to the
PL's table) does to a run, measured against the bit-identical default.

    sum_order_drift.py [--jobs N] [--threads T] suite/name ...

Per design, from the D6 config (d6_config.py), runs:
    base         the default: bit-identical to sw_only
    noise+/-     init_gamma * (1 +/- 2.5e-7), about 2 ULP: the noise floor. A placement run is chaotic, so
                 ANY perturbation moves the result; a variant is only meaningful against this scale.
    deposit, reductions, slot (both), lut, slot+lut     the [sw_flat] switches (#45 step 5b)
    snapshot, exact_base, Bprime                          #48's overflow map: B, the exact fixed base, both
    tree, stream, fixed   hpwl_gradient_CU's arithmetic, one switch each (record_stream.cpp)
    pl                    all of the PL's arithmetic: slot+lut, tree, stream, fixed
    pl+Bprime             pl plus #48's B': step 6's golden config
    pl+Bprime+qF          that, with bin_density_CU's fixed-point accumulator at F fraction bits (#48)
and prints, vs base: the first trace row that differs, iterations, and the final masked HPWL, exact
HPWL and exact overflow as relative differences. Runs in build/sum_order_drift/. This is a
measurement, not a pass/fail test: the verdict is read against the noise rows. Meow.
"""
import argparse
import concurrent.futures as futures
import os
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
PLACE = HERE.parent / "build" / "sw_flat_place"
WORK = HERE.parent / "build" / "sum_order_drift"

NOISE = {f"noise{k:+d}": {"gamma_scale": 1 + k * 2.5e-7} for k in (-3, -2, -1, 1, 2, 3)}
VARIANTS = {
    "base":       {},
    **NOISE,
    "deposit":    {"slot_order": ["deposit"]},
    "reductions": {"slot_order": ["reductions"]},
    "slot":       {"slot_order": ["deposit", "reductions"]},
    "lut":        {"wa_exp": "lut"},
    "slot+lut":   {"slot_order": ["deposit", "reductions"], "wa_exp": "lut"},
    "snapshot":   {"overflow_map": "snapshot"},
    "exact_base": {"fixed_base": "exact"},
    "Bprime":     {"overflow_map": "snapshot", "fixed_base": "exact"},
    "tree":       {"wa_sums": "tree"},
    "stream":     {"grad_merge": "stream"},
    "fixed":      {"hpwl": "fixed"},
    "pl":         {"slot_order": ["deposit", "reductions"], "wa_exp": "lut", "wa_sums": "tree",
                   "grad_merge": "stream", "hpwl": "fixed"},
    "pl+Bprime":  {"slot_order": ["deposit", "reductions"], "wa_exp": "lut", "wa_sums": "tree",
                   "grad_merge": "stream", "hpwl": "fixed", "overflow_map": "snapshot", "fixed_base": "exact"},
}
for _bits in (14, 16, 18):
    VARIANTS[f"pl+Bprime+q{_bits}"] = {**VARIANTS["pl+Bprime"], "deposit": "fixed", "deposit_frac_bits": _bits}


def variant_config(base_text, spec):
    text = base_text
    if "gamma_scale" in spec:
        value = float(re.search(r"^init_gamma\s*=\s*([0-9.eE+-]+)", text, re.M).group(1))
        text = re.sub(r"^init_gamma\s*=.*$", f"init_gamma = {value * spec['gamma_scale']!r}", text, count=1, flags=re.M)
    lines = []
    if "slot_order" in spec:
        lines.append("slot_order = [" + ", ".join(f'"{f}"' for f in spec["slot_order"]) + "]")
    for key in ("wa_exp", "overflow_map", "fixed_base", "wa_sums", "grad_merge", "hpwl", "deposit"):
        if key in spec:
            lines.append(f'{key} = "{spec[key]}"')
    if "deposit_frac_bits" in spec:
        lines.append(f"deposit_frac_bits = {spec['deposit_frac_bits']}")
    if lines:
        text += "\n[sw_flat]\n" + "\n".join(lines) + "\n"
    return text


def run(design, name, base_text, threads):
    out = WORK / design.replace("/", "_") / name
    out.mkdir(parents=True, exist_ok=True)
    config = out / "config.toml"
    config.write_text(variant_config(base_text, VARIANTS[name]))
    env = dict(os.environ, OMP_NUM_THREADS=str(threads))
    subprocess.run([str(PLACE), str(config), str(out)], check=True, env=env, stdout=subprocess.DEVNULL)
    return out


def read_final(out):
    final = {}
    for line in (out / "final.txt").read_text().splitlines():
        key, _, value = line.partition(" ")
        final[key] = value
    return final


def first_difference(a, b):
    rows_a, rows_b = (p.read_text().splitlines() for p in (a / "schedule_trace.csv", b / "schedule_trace.csv"))
    for i, (x, y) in enumerate(zip(rows_a[1:], rows_b[1:]), start=1):
        if x != y:
            return i
    return None if len(rows_a) == len(rows_b) else min(len(rows_a), len(rows_b))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("designs", nargs="+")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--variants", nargs="+", help="run only these (base and the noise rows always run)")
    args = parser.parse_args()

    if args.variants:
        unknown = set(args.variants) - set(VARIANTS)
        if unknown:
            parser.error(f"unknown variants {sorted(unknown)}")
        for name in [n for n in VARIANTS if n != "base" and n not in NOISE and n not in args.variants]:
            del VARIANTS[name]

    tasks = {}
    with futures.ThreadPoolExecutor(args.jobs) as pool:
        for design in args.designs:
            d6 = WORK / design.replace("/", "_") / "d6.toml"
            d6.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["python3", str(HERE / "d6_config.py"), design, str(d6.parent / "sw_only"), str(d6)], check=True)
            base_text = d6.read_text().replace("dump_schedule_trace = false", "dump_schedule_trace = true")
            for name in VARIANTS:
                tasks[(design, name)] = pool.submit(run, design, name, base_text, args.threads)

    # "in noise": the HPWL change lies within [min, max] of the noise runs' changes, base's own 0
    # included -- base is one sample of the same noise (bigblue4's six all fell below it). Meow.
    print("| design | variant | first differing row | iterations | stop | HPWL | in noise | HPWL exact | overflow exact | run s |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for design in args.designs:
        base_out = tasks[(design, "base")].result()
        base = read_final(base_out)
        def rel(f, key):
            return (float(f[key]) - float(base[key])) / float(base[key])
        noise_hpwl = [0.0] + [rel(read_final(tasks[(design, n)].result()), "final_hpwl") for n in NOISE]
        for name in VARIANTS:
            out = tasks[(design, name)].result()
            f = read_final(out)
            row = first_difference(base_out, out)
            cells = [design, name, "identical" if row is None else str(row), f["iterations"], f["stop_reason"]]
            if name == "base":
                cells += [f"{float(f['final_hpwl']):.4e}", f"noise {100 * min(noise_hpwl):+.3f}..{100 * max(noise_hpwl):+.3f}%",
                          f"{float(f['final_hpwl_exact']):.4e}", f"{float(f['final_overflow']):.4f}"]
            else:
                inside = min(noise_hpwl) <= rel(f, "final_hpwl") <= max(noise_hpwl)
                cells += [f"{100 * rel(f, 'final_hpwl'):+.3f}%", "" if name in NOISE else ("yes" if inside else "**no**"),
                          f"{100 * rel(f, 'final_hpwl_exact'):+.3f}%", f"{100 * rel(f, 'final_overflow'):+.2f}%"]
            cells.append(f["seconds_run"])
            print("| " + " | ".join(cells) + " |")


if __name__ == "__main__":
    main()
