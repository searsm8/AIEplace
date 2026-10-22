# REPORT #40 — Dhar's 16-pin net cap costs +12.4% post-DP HPWL; decompose exactly instead

*2026-09-18. Task #40 (`hpwl_gradient_dhar`). Question from Mark: `hpwl_gradient_dhar` ignores nets
with more than 16 pins, and Dhar (FPL 2019) claims that slightly improves quality. (1) What does
ignoring them cost us? (2) How hard is decomposing large nets instead?*

## TL;DR
- **Ignoring >16-pin nets is a large, uniform loss: 28/28 designs worse, mean +12.44% post-DP HPWL**
  (median +5.36%). ISPD2005 **+30.6%** (up to +51% on bigblue4), ISPD2015 **+5.2%**. Not one design
  improved.
- **Dhar's claim doesn't carry over to our benchmarks.** Their benchmarks are FPGA designs, where >16-pin nets are
  <0.25% of nets, and their "slightly better" result compares against a different placer ([7]), not an
  A/B with the cap as the only change. On ours, 17–100-pin nets are 2.4–3.3% of ISPD2005 nets
  but hold **20–27% of all pins**. The loss tracks that pin share.
- **Recommendation: exact chunking (option 2a below).** It computes the same math as the golden, in 16-pin
  blocks, so the golden already is its quality. It adds 6–9% more blocks on ISPD2005 and ≤3.8% on
  ISPD2015. It is the same restructure as #40's planned lane-narrowing, so do them together.

## 1. The A/B — approach 1 (ignore nets >16 pins)

**Setup.** `ignore_net_degree = 16` vs the frozen golden (`= 100`, XPlace's mask). **No code change**:
the knob already masks the WA gradient (`Partials.cpp`) *and* the HPWL metric (`BestSolution.cpp`,
`Output.cpp`), which is exactly what the Dhar kernel does too. It drops a big net's gradient and its HPWL
by-product. `make test-regress` bit-identical before the run (HEAD = frozen `271d024` behaviour).

- Arm B: `python3 tools/dse.py --designs tier1+tier2 --set ignore_net_degree=16` →
  `vck5000/results/DSE_20260918_124001/` (28/28, seeds/grids/td from `benchmarks.py`, as golden).
- Arm A: [[GOLDEN_sw_only_frozen_20260825]] `dse_results.csv`.
- Compared only on **mask-independent** metrics. The main one is post-DP HPWL (XPlace's own LG+DP, all nets,
  legal-vs-legal); exact GP HPWL over all nets is the other. "Best GP HPWL" is masked at each arm's own
  cap and is **not** comparable across arms.
- Scripts: `.claude/2_ARTIFACTS/nd16/{compare,net_degree_stats,chunk_stats}.py`.

| design | pins on 17–100-pin nets | ΔDP HPWL | DP ratio vs XPlace golden → cap16 |
|---|---:|---:|---|
| adaptec1 | 20.9% | +20.08% | 1.0010 → 1.2020 |
| adaptec2 | 25.5% | +28.66% | 1.0061 → 1.2945 |
| adaptec3 | 26.7% | +28.32% | 0.9956 → 1.2776 |
| adaptec4 | 22.8% | +17.40% | 1.0109 → 1.1869 |
| bigblue1 | 20.1% | +16.66% | 1.0042 → 1.1714 |
| bigblue2 | 23.0% | +44.24% | 1.0052 → 1.4498 |
| bigblue3 | 21.5% | +38.62% | 1.0127 → 1.4038 |
| bigblue4 | 26.4% | +51.05% | 1.0096 → 1.5249 |
| mgc_des_perf_1 / _a / _b | 0.6% | +0.28 / +0.19 / +0.19% | ≈ unchanged |
| mgc_edit_dist_a | 1.9% | +1.31% | 1.0093 → 1.0225 |
| mgc_fft_1 / _2 / _a / _b | 3.2–3.3% | +1.87 / +2.29 / +2.00 / +1.60% | ~+0.02 each |
| mgc_matrix_mult_1 / _2 | 1.1% | +3.15 / +2.19% | |
| mgc_matrix_mult_a / _b / _c | 1.1% | +4.99 / +5.24 / +5.45% | |
| mgc_pci_bridge32_a / _b | 8.1 / 7.9% | +8.54 / +5.28% | 1.0215 → 1.1087 · 0.9822 → 1.0341 |
| mgc_superblue11_a | 11.9% | +10.09% | 1.0203 → 1.1233 |
| mgc_superblue12 | 7.9% | +19.37% | 1.0465 → 1.2492 |
| mgc_superblue14 | 13.4% | +14.34% | 1.0273 → 1.1747 |
| mgc_superblue16_a | 10.8% | +4.07% | 1.0303 → 1.0722 |
| mgc_superblue19 | 10.0% | +10.78% | 1.0793 → 1.1957 |

| suite | n | mean ΔDP | median ΔDP | better | mean DP ratio: golden → cap16 |
|---|---:|---:|---:|---:|---|
| ISPD2005 | 8 | **+30.63%** | +28.49% | 0/8 | 1.0057 → 1.3139 |
| ISPD2015 | 20 | **+5.16%** | +3.61% | 0/20 | 1.0139 → 1.0669 |
| all | 28 | **+12.44%** | +5.36% | 0/28 | 1.0115 → 1.1375 |

Iterations and runtime are essentially unchanged (±5%). The cap costs quality and does not buy
convergence. Pin-share column from `net_degree_stats.py` (parses the `.nets` / `floorplan.def`
netlists directly; degree-1 nets excluded).

**Why it hurts.** A 17–100-pin net is not a die-spanning clock (those are >100 and already masked).
It's ordinary logic fanout. Dropping it leaves those cells with no wirelength pull toward
each other, and density spreads them freely. The loss scales with the share of pins that go free, not the share
of nets.

**Falsifier.** Re-run arm B at `--set ignore_net_degree=16,32,64` to get the loss-vs-cap curve. If
the curve is not monotone in cap, the "pins going free" explanation is wrong.

## 2. Approach 2 — decomposing large nets: estimate

### 2a. Exact chunking (recommended)
The WA gradient of pin *k* depends on its own coordinate plus a few whole-net values: the bbox (max/min) and
four sums per axis, B± = Σa±, C± = Σx·a±. All are **associative**, so a d-pin net can run through the
same 16-lane datapath in ⌈d/16⌉ blocks:

1. **load** all pins into an on-chip net buffer (≤112 = 7×16 slots, since host already masks >100),
   reducing the bbox on the way in (`load_net` already does this);
2. **sum pass**: per block, `term_gen` + adder trees → add into 8 per-net accumulators (≤7 iterations);
3. **gradient pass**: per block, combiners with the net totals → scatter.

A ≤16-pin net is the 1-block case, so it's **one datapath**, not a fast path plus a slow path.

- **Cost** (`chunk_stats.py`, blocks for 17–100-pin nets relative to 2..16-pin nets): **ISPD2005
  +6.0–8.9%**, superblue +2.5–3.8%, other ISPD2015 +0.2–2.3%. With two passes over big-net blocks,
  a worst case of ~12–18% more `net_loop` cycles on ISPD2005.
- **Quality: no experiment needed.** The arithmetic is identical to `computeHpwlPartials_CPU`, so the frozen
  golden *is* this option's quality. Verification is purely numerical: repoint `hpwl_dhar_test`'s golden from
  "capped" to the uncapped one `hpwl_gradient_test` uses, and add test nets at block boundaries (17, 32,
  33, 100 pins).
- **Host: nothing.** The CSR already carries any degree; the 100 mask is host-side already.
- **Simplifies the kernel:** phase Z (zero `pin_grad_DDR`) exists *only* because of the cap. Every live pin is
  written once again, so it can go.
- **Size:** ~100–150 lines in `hpwl_gradient_dhar.hpp`. `net_loop` becomes a loop over blocks with a small
  per-net state machine (load / sum / gradient).
- **Risk is timing, and it overlaps with #40.** The buffer and accumulator muxes add to the `net_loop` region
  that already fails P&R. But #40's planned fix — waves of W=4–8 lanes instead of 16 at once — *is*
  "a net takes ⌈d/W⌉ waves". One restructure solves both. Do not do them as two refactors.
- **Effort:** ~1–2 days to tier-1 + clean C-synth. P&R closure is the unknown (hours per build), and
  it's shared with #40 regardless.

### 2b. Hybrid (fallback)
Keep Dhar for ≤16-pin nets and run the existing arbitrary-degree `hpwl_gradient` module (already
verified) on 17–100-pin nets. Both scatter into the same node-major `pin_grad_DDR`, and one phase 3 sums
them. It's exact and needs the least new logic (~½ day of wiring), but it puts two gradient datapaths on the device
and streams big-net pins from DDR three times. Use it if arbitrary-degree support is needed on device before
#40 closes.

### 2c. Topological decomposition — Dhar's literal suggestion (not recommended)
"Break into ≤16-pin sub-nets with a common driver":
- **Changes the objective.** Σ sub-net WA-HPWL ≠ the net's HPWL, so it diverges from XPlace and needs its
  own quality A/B like §1.
- **Needs driver pins we don't have.** Only `IOPad` has a direction; `NetPin` has none, so it needs
  parser work (LEF pin `DIRECTION`, bookshelf `I/O` flags).
- **Has no meaningful static grouping.** All cells start stacked at the centre, so grouping by
  position needs periodic host re-clustering, which fights the device-resident loop.
- **Effort:** days of software before any hardware, with an uncertain outcome.

## Incidental fixes made to run this
- `vck5000/tools/lgdp.py:54` — `REPO / "host/benchmarks"` → `REPO.parent / "host/benchmarks"`.
  `68f9b38` (host/ → repo root) missed it, and every bookshelf (ISPD2005/MMS) LG+DP crashed with
  `FileNotFoundError …/vck5000/host/benchmarks/…/adaptec1.aux`.
- `~/phd/Xplace/data/raw/{ispd2005,ispd2015,mms}` symlinks retargeted from
  `…/vck5000/host/benchmarks/` to `…/host/benchmarks/` (outside the repo). The same move broke
  them, and ISPD2015 LG+DP crashed on `floorplan.def`.
