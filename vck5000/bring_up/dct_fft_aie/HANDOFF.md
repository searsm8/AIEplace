# HANDOFF — rebuild `dct_fft_aie` for the hw node

**You are (or should be) on the build node.** Do not build on the hw node
(`hacc-gpu-u55c-01`) — it has no Vitis 2022.2/2024.2 build capacity reserved
for this and shouldn't be used for `v++`/`aiecompiler` runs if we can help it.
It's for running the resulting xclbin on real VCK5000 hardware only.

## UPDATE 2026-08-27 — the 2024.2 rebuild below is a dead end; do not attempt it

Tried it on the build node. `v++ 2024.2` **refuses the platform outright**,
before doing any compilation:

```
ERROR: [v++ 60-1299] The specified platform is not supported. Platform
'xilinx_vck5000_gen4x8_qdma_2_202220_1.xpfm' (version 2022.2) is not
supported by the current tool version (2024.2). By policy, platforms are
supported for the remainder of the calendar year release plus the following
calendar year release
```

This is a hard policy wall in the tool, not a config/path problem — no amount
of flag-juggling gets past it. **2024.1 is presumably blocked the same way**
(same "current + following year" policy relative to a 2022.2 platform;
not separately tested since we confirmed 2024.2 fails first).

Also confirmed by exhaustive search (`/opt`, every `/tools/Xilinx/Vitis/*/base_platforms`,
`/pub/scratch`): **there is no newer VCK5000 platform anywhere on this
system, and AMD never published one** — `xilinx_vck5000_gen4x8_qdma_2_202220_1`
(2022.2-generation) is the only VCK5000 `.xpfm` that exists, full stop. So this
isn't a "find the right platform package" problem either — the platform itself
is frozen at 2022.2.

**Net effect: the buildable tool-version window for this platform is 2022.2
through 2023.2 inclusive** (Xilinx's "current release + following calendar
year" policy, counting from the platform's 2022.2 vintage) — verified
empirically, not just read off the policy text: `v++ 2023.2` accepts the
platform and HLS-synthesizes `top.cpp` cleanly (RTL model generation
completed without error); `v++ 2024.2` rejects it before touching the source
at all. **2024.x is categorically not an option no matter what's installed on
the hw node's driver side** — if the hw node's XRT needs a 2024-generation
AIE CDO/PDI to load correctly, that need has to be solved some other way
(older XRT on the hw node, or accepting the format 2022.2/2023.2 produce),
because the build side cannot move to 2024.x for this platform.

Available Vitis versions on the build node, for reference: 2022.1, 2022.2,
2023.2, 2024.1, 2024.2 (all under `/tools/Xilinx/Vitis/<ver>/`), each with a
matching `Model_Composer/<ver>/tps/xf_dsp` DSPLib and (mostly) a matching
`/opt/xilinx/xrt_<ver>` — so 2023.2 is a fully available, drop-in alternative
to 2022.2 for everything below, just substitute the version.

**Update 2: the 2023.2 rebuild was tried and it doesn't build at all** — this
closes off the toolchain-version-bump approach entirely, don't retry it.

Ran `TARGET=hw all` with `XILINX_VITIS=/tools/Xilinx/Vitis/2023.2`,
`DSPLIB_ROOT=.../Model_Composer/2023.2/tps/xf_dsp`,
`XILINX_XRT=/opt/xilinx/xrt_2023.2`. PL HLS synthesis and the full AIE compile
(`aiecompiler --target=hw`, all 8 lanes, `libadf.a` generated) both succeeded
cleanly — same as 2022.2. It fails later, in the `v++ -l` link stage, during
Vivado **placement**, specifically NoC (Network-on-Chip) place-and-route:

```
ERROR: [Ipconfig 75-177] Unable to combine groups for destination ID generation
ERROR: [Ipconfig 75-175] Could not calculate design based destination IDs
ERROR: [Ipconfig 75-402] Destination ID Generation failed.
ERROR: [Ipconfig 75-656] NoC place and route failed
ERROR: [Place 30-1875] NOC compiler unsuccessful. Cannot place NOC instances
WARNING: [Place 30-1855] Cannot find AIE NOC AXI instance connected to
  top_i/ulp/axi_noc_kernel0/inst/{M02_INI_stub_nmu,S00_AXI_nmu,S01_AXI_nmu}/...
ERROR: [Place 30-99] Placer failed with error: 'NoC placement failed!'
```

(Full trace: `build/hw/logs/link/vivado.log` from the 2023.2 attempt, if still
on disk — not preserved past that run since the next rebuild overwrites
`build/hw/`.) Reads as a genuine structural incompatibility between 2023.2's
NoC placer and this platform's (2022.2-generation) NoC/shell metadata — not a
flag or config problem, and not something to retry as-is.

**So: of the three tool generations tried, 2022.2 is the only one that
successfully produces a complete xclbin for this platform at all.**
2023.2 fails mid-build (NoC placement); 2024.x is blocked before it starts
(platform-support policy, see Update 1). **Conclusion: the toolchain-version
route is exhausted — do not try another Vitis version on the build side.**
The original load-time failure on the hw node (`err -22`, AIE CDO/PDI
suspected) has to be solved on the hw node / runtime side instead: an older
XRT install there (matching 2022.2, if one exists alongside the 2024.2 one —
worth checking once card time is available, the same way this build node
turned out to have 2022.2/2023.2/2024.1/2024.2 all side-by-side), or by
following the original "If the 2024.2 rebuild still fails" checklist below
(now: "if no further rebuild is possible") — real `dmesg` detail, diffing
`AIE_METADATA`/`PARTITION_METADATA` sections, checking AIE resource counts —
using the existing 2022.2-built `build/hw/dct_fft_aie.hw.xclbin` as the
artifact under test, since that's the only one that exists.

**Update 3: `build/hw/dct_fft_aie.hw.xclbin` has been restored** — it got
deleted (`rm -rf build/hw`) partway through the 2024.2/2023.2 testing above
and needed a clean 2022.2 rebuild to put back. Two things worth recording
from that rebuild:

- **First rebuild attempt failed, transiently, not a regression.** HLS IP
  export choked: `ERROR: [Project 1-202] Error writing the XML file
  '.../tmp.xpr'` (`build/hw/_x/top.hw/top/vitis_hls.log`), even though the
  file existed on disk afterward with plausible content — looks like a
  write/consistency-check race, not a real I/O failure. At the time, this
  **build node was under heavy load from other users**: `uptime` showed load
  average ~34, `free -h` showed ~287/376 GiB RAM in use with swap nearly
  full, and `ps aux` showed several other users' large Vivado/v++ jobs
  running concurrently (2024.2, 2025.2). Worth checking system load
  (`uptime`, `free -h`) before/if a build fails oddly on this node — it's
  shared, not dedicated.
- **Retried immediately with identical flags, no source changes, and it
  built clean through all 6 Vivado tasks with 0 errors** (PL synth 4m48s, AIE
  compile clean, link+impl 1h56m, package 19s — placement in particular,
  the exact stage 2023.2 died on, completed in 23m03s with no NoC issues).
  Same-size output xclbin (10,825,882 bytes, matching the original 2026-08-27
  00:49 build) at `build/hw/dct_fft_aie.hw.xclbin`, timestamped 2026-08-27
  21:07. So: 2022.2 builds this platform reproducibly; the one failure was
  this shared node being busy, not a flaw in our design or a discovered
  incompatibility. If a `TARGET=hw` build fails with an obscure Vivado/HLS
  IP-export or Tcl error on the first try, **retry once before treating it as
  a real bug** — check `uptime`/`free -h` first if it's not obviously a
  source/config problem.

Command used (same as Update 1/the original section below, 2022.2 explicitly):
```bash
source /tools/Xilinx/Vitis/2022.2/settings64.sh
make TARGET=hw all \
  PLATFORM_REPO=/opt/xilinx/platforms \
  XILINX_XRT=/opt/xilinx/xrt_2024.2   # host build only; doesn't affect the xclbin itself
```

---

## Original handoff below (superseded where it says "rebuild with 2024.2" — see updates above)

## The problem

`build/hw/dct_fft_aie.hw.xclbin` (committed-adjacent build output, built
2026-08-27 00:49 by `v++ (2022.2)`) **fails to load** on `hacc-gpu-u55c-01`'s
VCK5000 cards:

```
[XRT] ERROR: See dmesg log for details. err = -22
terminate called after throwing an instance of 'xrt_core::system_error'
  what():  failed to load xclbin: Invalid argument
```

Reproduced consistently via `./run_hw.sh dct` (also tried `idct`/verbose XRT
logging — same `-22` every time, no more detail obtainable without root
`dmesg` access on that node).

## What's already been ruled out

The hw node itself is healthy and *not* the cause:

- Two VCK5000 cards present and `Device Ready: Yes`
  (`0000:c1:00.1`, `0000:e1:00.1`, shell `xilinx_vck5000_gen4x8_qdma_base_2`).
- `xbutil validate --device 0000:c1:00.1` **passes all tests**, including "aie".
- The card's flashed shell **interface UUID matches** the xclbin's expected
  interface UUID exactly (`eaae3fb8-b262-b65b-21fe-c0676792ebfc`) — not a
  base-shell/platform mismatch.
- XRT 2024.2 + the matching platform (`xilinx_vck5000_gen4x8_qdma_2_202220_1`)
  are installed on the hw node.

## Root cause (best evidence, not 100% confirmed)

It's specifically the **AIE array config (CDO/PDI) download**, not the base
bitstream. Comparison:

| xclbin | built by | sections | loads on hw node? |
|---|---|---|---|
| `/opt/xilinx/firmware/vck5000/gen4x8-qdma/base/test/verify.xclbin` (XRT's own validate test image) | v++ 2022.2, same day/platform/interface-UUID as ours | **no** `AIE_METADATA`/`AIE_RESOURCES` | yes (that's why `validate` passes) |
| `dct_fft_aie.hw.xclbin` (ours) | v++ 2022.2 | has `AIE_METADATA` + `AIE_RESOURCES` + `BITSTREAM_PARTIAL_PDI` | **no** |

So `xbutil validate`'s "aie" test never actually exercises AIE-graph
downloading — it's a false positive for our purposes. The working theory is
that the 2022.2-generation `aiecompiler`/`v++` AIE partition (CDO) format
isn't accepted by this node's 2024.2 driver stack for the AIE-array config
step. Rebuilding with the 2024.2 toolchain (matching the hw node's driver)
is the fix to try.

## What to do

1. **Rebuild `TARGET=hw` with Vitis/Vivado 2024.2** on the build node (not
   2022.2 — that's what produced the currently-broken xclbin):

   ```bash
   cd dct_fft_aie
   source /tools/Xilinx/Vitis/2024.2/settings64.sh
   source /opt/xilinx/xrt/setup.sh
   make TARGET=hw all \
     XILINX_VITIS=/tools/Xilinx/Vitis/2024.2 \
     DSPLIB_ROOT=/tools/Xilinx/Model_Composer/2024.2/tps/xf_dsp
   ```

   The `Makefile` defaults (`XILINX_VITIS ?= /tools/Xilinx/Vitis/2022.2`,
   `DSPLIB_ROOT ?= .../Model_Composer/2022.2/tps/xf_dsp`) still point at
   2022.2 — override both, or edit the Makefile if you're standardizing on
   2024.2 going forward. Confirmed on the hw node that
   `Model_Composer/2024.2/tps/xf_dsp` exists with the same layout as 2022.2's,
   so the path swap should be a drop-in.

2. **Check `PLATFORM_REPO` before building.** The Makefile defaults to
   `$(HOME)/xilinx_local/opt/xilinx/platforms/xilinx_vck5000_gen4x8_qdma_2_202220_1/...xpfm`
   — this did **not** exist under `$HOME` on the hw node (checked; not
   present there, might be build-node-local only). Confirm it exists on the
   build node, or point `PLATFORM` explicitly at
   `/opt/xilinx/platforms/xilinx_vck5000_gen4x8_qdma_2_202220_1/xilinx_vck5000_gen4x8_qdma_2_202220_1.xpfm`
   if that's where it actually lives there (that's the path used on the hw
   node, under `/opt` not `$HOME`).

3. **Sanity-gate cheaply first** (per README), don't jump straight to the
   full hw build:
   ```bash
   make golden        # pure g++, seconds
   make csynth         # v++ -c only, minutes
   make TARGET=sw_emu run   # full sw_emu, confirms functional correctness
   make TARGET=hw all       # the real build — this is the one that matters here
   ```

4. **Hand the new `build/hw/` back to the hw node** (same path,
   `/home/msears/AIEplace/dct_fft_aie/`, appears to be on shared storage
   reachable from both nodes — verify) and re-run there:
   ```bash
   ./run_hw.sh dct
   ```
   Expected on success: `TEST PASSED  transform=dct  rel_rms=<small> (tol 1.0e-03)`.

   Note: `run_hw.sh` also defaults `XILINX_VITIS` to `/tools/Xilinx/Vitis/2022.2`
   for its runtime `LD_LIBRARY_PATH`, which doesn't exist on the hw node
   either — override it there too:
   ```bash
   XILINX_VITIS=/tools/Xilinx/Vitis/2024.2 ./run_hw.sh dct
   ```

## If the 2024.2 rebuild *still* fails to load on hw

Then the AIE-CDO theory above is wrong and it's something else — next
things to check, in order:
- Get real `dmesg`/kernel log detail on the hw node (needs a user with
  `adm`/root access; the agent user there could not read `/var/log/kern.log`
  or run `sudo dmesg`, no tty for sudo in that session).
- Diff `AIE_METADATA`/`PARTITION_METADATA` sections between a known-working
  AIE xclbin (if one can be found/built for this exact platform+XRT combo)
  and ours via `xclbinutil --dump-section`.
- Check whether the platform's AIE array resource count/config in our
  `graph.cpp` (`DensityFFTGraph`, 8-lane pool) exceeds what's actually
  physically available/free on the card.
