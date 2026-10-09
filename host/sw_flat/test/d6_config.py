#!/usr/bin/env python3
"""d6_config.py -- write the D6 config (#45 plan) for one design: sw_only's phase-1 run that sw_flat
must reproduce bit for bit.

    d6_config.py <suite/name> <results_dir> <out.toml>

The base is the design's frozen regress config (vck5000/test/regress/configs/<name>.toml) when one
exists, else mgc_fft_a's with the benchmark, target density and grid from tools/benchmarks.py (a
bare config silently gets those wrong). Then D6: no backtracking, no phase 2, nets <= 96 pins, and
the schedule trace on. The benchmark path is made absolute so either placer can run from anywhere.
Meow.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
REGRESS = REPO / "vck5000" / "test" / "regress" / "configs"
sys.path.insert(0, str(REPO / "vck5000" / "tools"))
import benchmarks  # noqa: E402


def set_key(text, section, key, value):
    """Replace `key = ...` inside [section], or add it right after the header."""
    header = re.search(rf"^\[{section}\]\s*$", text, re.M)
    end = re.search(r"^\[", text[header.end():], re.M)
    body_end = header.end() + (end.start() if end else len(text) - header.end())
    body = text[header.end():body_end]
    line = f"{key} = {value}"
    if re.search(rf"^{key}\s*=", body, re.M):
        body = re.sub(rf"^{key}\s*=.*$", line, body, count=1, flags=re.M)
    else:
        body = "\n" + line + body
    return text[:header.end()] + body + text[body_end:]


def main():
    design, results_dir, out = sys.argv[1], sys.argv[2], sys.argv[3]
    entry = benchmarks.BENCHMARKS[design]
    base = REGRESS / f"{entry['name']}.toml"
    text = (base if base.exists() else REGRESS / "mgc_fft_a.toml").read_text()
    if not base.exists():
        text = set_key(text, "params", "maximum_utilization", entry["target_density"])
        text = set_key(text, "params", "bins_per_row", entry["grid"])
    text = set_key(text, "input", "benchmark", f'"{REPO / "host" / "benchmarks" / design}"')
    text = set_key(text, "params", "enable_backtracking", "false")
    text = set_key(text, "params", "enable_phase2", "false")
    text = set_key(text, "params", "ignore_net_degree", 96)
    text = set_key(text, "output", "dump_schedule_trace", "true")
    text = set_key(text, "output", "results_dir", f'"{results_dir}"')
    Path(out).write_text(text)


if __name__ == "__main__":
    main()
