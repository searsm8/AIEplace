#!/usr/bin/env python3
"""Re-process a finished sweep without re-running it: rebuild dse_results.csv from gp_only.csv +
lgdp.json + the current tools/benchmarks.py references, and print the aggregate ratios.

    python3 tools/analyze_dse.py results/DSE_<ts>          # or .../dse_results.csv

Useful after a benchmarks.py reference changes — the per-design table is dse_results.csv itself.
Calls dse.py::summarize, the one implementation.
"""
import os
import sys

from dse import summarize

path = sys.argv[1] if len(sys.argv) > 1 else "."
summarize(os.path.dirname(path) if os.path.isfile(path) else path.rstrip("/"))
