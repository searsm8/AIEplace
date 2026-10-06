#!/usr/bin/env python3
"""sampler_report.py -- aggregate a sampler.hpp profile.
   sampler_report.py <binary> <samples file> [top_n]
Prints SELF (innermost frame) and INCLUSIVE (anywhere on the stack) percentages per function.
Executable addresses are resolved with addr2line; inlined code is attributed to the function it
was inlined into AND the inlined function (addr2line -i gives the chain). Meow."""
import collections
import subprocess
import sys

binary, path = sys.argv[1], sys.argv[2]
top_n = int(sys.argv[3]) if len(sys.argv) > 3 else 30
samples = [line.split() for line in open(path) if line.strip()]
# Every frame but the innermost is a RETURN address -- the instruction after the call, which can
# already belong to the next inlined region. Resolve those at address-1, inside the call. Meow.
samples = [[s[0]] + [hex(int(f, 16) - 1) if f.startswith("0x") else f for f in s[1:]] for s in samples if s]
addrs = sorted({f for s in samples for f in s if f.startswith("0x")})

names = {}
if addrs:
    # -a prints each address before its chain; -i gives the inline chain innermost first, as
    # (function, file:line) pairs, ending in the real (non-inlined) function. Meow.
    out = subprocess.run(["addr2line", "-a", "-f", "-C", "-i", "-e", binary] + addrs,
                         capture_output=True, text=True).stdout.splitlines()
    current = None
    for line in out:
        if line.startswith("0x") and ":" not in line:
            current = "0x" + line[2:].lstrip("0")
            names[current] = []
        elif current is not None:
            names[current].append(line)
    for key, chain in names.items():
        names[key] = chain[0::2] or ["??"]
    # the sampler wrote %p (no leading zeros); normalise the lookup keys the same way
    addrs_norm = {a: "0x" + a[2:].lstrip("0") for a in addrs}
    names = {a: names.get(addrs_norm[a], ["??"]) for a in addrs}

def strip_templates(name):
    out, depth = [], 0
    for c in name:
        if c == "<":
            depth += 1
            if depth == 1:
                out.append("<>")
        elif c == ">":
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(c)
    return "".join(out)

def frames(sample):
    for f in sample:
        for name in (names.get(f, [f]) if f.startswith("0x") else [f]):
            yield strip_templates(name.replace("(anonymous namespace)", "anon")).split("(")[0][:110]

# ONLY=<function>: keep samples with that frame on the stack -- e.g. ONLY=main for the main
# thread, which is the critical path once worker threads share the samples. Meow.
import os
only = os.environ.get("ONLY")
self_count, incl_count = collections.Counter(), collections.Counter()
kept = 0
for s in samples:
    chain = list(frames(s))
    if not chain or (only and only not in chain):
        continue
    kept += 1
    self_count[chain[0]] += 1
    for name in set(chain):
        incl_count[name] += 1

total = max(kept, 1)
print(f"{kept} of {len(samples)} samples" + (f" (stacks through {only})" if only else ""))
print("--- SELF")
for name, n in self_count.most_common(top_n):
    print(f"{100.0 * n / total:6.1f}%  {name}")
print("--- INCLUSIVE")
for name, n in incl_count.most_common(top_n):
    print(f"{100.0 * n / total:6.1f}%  {name}")
