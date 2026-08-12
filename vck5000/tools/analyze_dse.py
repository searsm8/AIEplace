#!/usr/bin/env python3
"""Re-render a finished sweep's table without re-running it.

    python3 tools/analyze_dse.py results/DSE_<ts>          # or .../results.csv

The renderer itself lives in dse.py::summarize -- one implementation, so the table you get
here is the one the sweep printed. A sweep dir from before 2026-08-12 has no sweep.json;
the result columns still render, only the swept-parameter columns are missing.
"""
import os
import sys

from dse import summarize

path = sys.argv[1] if len(sys.argv) > 1 else "."
summarize(os.path.dirname(path) if os.path.isfile(path) else path.rstrip("/"))
