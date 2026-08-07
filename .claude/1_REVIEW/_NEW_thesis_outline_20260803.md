# AIEplace thesis outline (v1, 2026-08-03)

Structured after M. Klaisoongnoen, *Optimisation Strategies for Quantitative Finance on
Reconfigurable Architectures* (Edinburgh, 2026) — hereafter **[MK]** — with the quantitative
finance domain replaced by analytical VLSI global placement (ePlace).

Decisions taken 2026-08-03:
1. **Breadth axis** = the four kernels of one placement iteration (not circuit designs, not
   four applications). Designs are the *evaluation sweep* inside each results section.
2. **Energy is first-class.** VCK5000 power measurement must be solved early — [MK] §9.1 admits
   he could not get device-side energy on this exact card.
3. **`sw_only` golden folds into the Ch4 baseline chapter**, with faithfulness/validation as a
   major section rather than its own chapter.

Status legend: **[E]** work exists · **[P]** partially exists · **[B]** must be built · **[M]** must be measured

---

## 1. Introduction (~6 pp)

- 1.1 Research hypothesis and objectives
- 1.2 Contributions
- 1.3 Published papers
- 1.4 Structure of thesis

### Hypothesis

Analytical global placement — an iterative, feedback-controlled numerical optimisation — can be
mapped onto a heterogeneous reconfigurable device (Versal PL + AI Engines) to deliver
performance and energy-efficiency advantages over CPU implementations, without degrading
solution quality relative to a state-of-the-art reference.

The "without degrading solution quality" clause is the load-bearing difference from [MK]. His
benchmarks have one correct answer; placement trades runtime × energy × **quality**.

### Research questions (stated here, answered as literal headings in Ch9 — [MK]'s device)

1. **What algorithmic techniques best suit the acceleration of an iterative, coupled numerical
   optimisation when moving from a von Neumann CPU implementation to dataflow architectures?**
   ([MK] RQ1 adapted — his four benchmarks were independent and embarrassingly parallel; a
   placement iteration is a feedback loop whose kernels are mutually dependent.)
2. **Where should the loop and its state live on a heterogeneous PL+AIE device when the dataflow
   graph is inherently cyclic?** Covers both device residency (Ch7) and the PL/AIE partition
   (Ch8). This is the question [MK] Ch8 opened and could not answer.
3. **How do numeric representation and precision affect runtime, power and resource utilisation
   — and the quality of the resulting placement?** ([MK] RQ2, extended by the quality axis.)
4. **What limits the acceleration of global placement on reconfigurable architectures, and what
   would be needed to overcome them?** ([MK] RQ4.)

> [MK]'s RQ3 (portability between vendors, Xilinx ↔ Intel) has no analogue here — single
> platform. Its slot in the argument is taken by RQ2's partitioning question.

### Contributions (draft)

- A **faithful software reimplementation of the XPlace formulation** of ePlace, validated
  design-by-design against the reference on ISPD2005 and MMS, serving as the golden reference
  against which every hardware module is verified. (No analogue in [MK], who inherited
  QuantLib/VQFL.)
- **Dataflow architectures for the four kernels of a placement iteration**, with a transformation
  taxonomy applied across all four, and per-kernel verification against the golden.
- **A device-resident iteration loop**: schedule, convergence test and Nesterov/BB state held on
  the PL, eliminating the per-iteration host round-trip. Contrast with [MK] Ch7, which *hides*
  transfer latency; this *removes* the transfer.
- **A PL/AIE partitioning for a cyclic algorithm** that confines the AIEs to the acyclic kernels
  they suit (spectral solve, wirelength gradient) while the cycle is closed entirely in PL —
  directly addressing the limitation reported in [MK] §9 RQ4.
- **A precision study for an optimisation algorithm**, where the error metric is degradation of
  the objective (HPWL/overflow trajectory), not deviation of a scalar result.

### Publication spine — **[B], needs planning**

[MK] stapled 6 papers onto Chs 5–8; each chapter inherited a self-contained §X.1 Experimental
setup from its paper. Worth deciding early what the paper units are. Natural candidates:
(i) the faithful golden + validation methodology; (ii) the PL dataflow architecture + per-kernel
results; (iii) device residency; (iv) PL/AIE partitioning for cyclic graphs.

---

## 2. Reconfigurable architectures (~22 pp) — *the machine*

Closely tracks [MK] Ch2; ~80% reusable in structure. Additions marked.

- 2.1 Programmable Logic
  - 2.1.1 LUTs, CLBs, logic elements
  - 2.1.2 DSP slices
  - 2.1.3 **On-chip memory hierarchy: BRAM, URAM, and external DDR** *(expanded vs [MK] — the
    DDR-residency of the 1024×1024 matrices is a central design constraint, so the memory
    hierarchy has to be established here)*
  - 2.1.4 Arbitrary integer and fixed-point data types
- 2.2 Code transformations in High-Level Synthesis
  - 2.2.1 Pipelining and initiation interval
  - 2.2.2 Dataflow, streams, FIFO vs PIPO
  - 2.2.3 Unrolling and array partitioning
- 2.3 Coarse-Grained Reconfigurable Architectures: the Versal ACAP
  - 2.3.1 AI Engine core and vector unit
  - 2.3.2 Programming model (ADF graph, kernels, PLIO/GMIO)
  - 2.3.3 Data communication between PL and AIE
  - 2.3.4 **The acyclic dataflow-graph constraint** *(plant the seed here — this is what RQ2
    turns on, and it is where [MK] Ch8 hit its wall)*
- 2.4 **The VCK5000 platform and the three-step Versal build flow** (`v++ -c` → `-l` → `-p`)
  *(new — [MK] used an Alveo U280 for most of his work with a simpler flow)*

**Status: [B]** — writing only, no experiments. Cheapest chapter to draft first.

---

## 3. VLSI global placement (~18 pp) — *the problem*

Mirrors [MK] Ch3, including his choice to put the related-work survey in the domain chapter
rather than a standalone chapter.

- 3.1 The placement problem: netlist, die, rows, legality, the HPWL objective
- 3.2 Analytical placement
  - 3.2.1 Wirelength smoothing: the weighted-average (WA) model and γ
  - 3.2.2 Density as a constraint: bins, target density, overflow
- 3.3 ePlace: the electrostatic analogy
  - 3.3.1 Poisson's equation and the electric field
  - 3.3.2 The spectral solve: DCT, IDCT, IDXST
  - 3.3.3 Nesterov acceleration and the Barzilai–Borwein step
  - 3.3.4 The parameter schedule: γ, λ, and the convergence test
- 3.4 Mixed-size placement: movable macros, fillers, and the two-phase flow
- 3.5 Benchmarks and metrics: ISPD2005, MMS, HPWL and overflow conventions
- 3.6 Related work
  - 3.6.1 ePlace, RePlAce, DREAMPlace, XPlace
  - 3.6.2 GPU acceleration of placement
  - 3.6.3 FPGA and reconfigurable acceleration in EDA
  - 3.6.4 **[MK] and the cyclic-graph limitation on AIEs** *(cite as the motivating negative
    result for RQ2)*

**Status: [B]** — writing only. §3.4 and §3.5 draw directly on TODO #4/#8/#11/#13 findings.

---

## 4. A software golden reference for analytical placement (~28 pp)

[MK] Ch4's slot: characterise the workload, establish the CPU performance and energy baseline,
profile it to identify bottlenecks. Extended with the faithfulness/validation work, which has no
analogue in [MK].

- 4.1 Experimental setup — CPU, OpenMP, RAPL energy capture, 5-run averaging
  - *note the OpenMP thread-count cliff (one CPU deliberately reserved from the team)*
- 4.2 `sw_only`: implementing the placement iteration
- 4.3 **Faithfulness to the reference: a validation methodology**
  - 4.3.1 Field normalisation and the preconditioner
  - 4.3.2 Filler area and the density footprint
  - 4.3.3 Overflow conventions: sharp vs smoothed, with/without fillers, with/without macros
  - 4.3.4 Stop criteria and the two-phase mixed-size flow
  - 4.3.5 What "faithful" bought: the 16-design A/B evidence
- 4.4 Quality baseline: `sw_only` vs XPlace on ISPD2005 and MMS
- 4.5 Performance and energy baseline on CPU
- 4.6 **Kernel profile: where the time goes** — introduces the four kernels that structure Chs 5–8:
  1. **HPWL gradient** (WA partials, scatter-accumulate over pins)
  2. **Density solve** (bin scatter → spectral transform → force gather)
  3. **Iteration update** (Nesterov + Barzilai–Borwein step)
  4. **Parameter schedule** (γ/λ update, convergence test)
- 4.7 Analysis: arithmetic intensity, memory behaviour, available parallelism, and the
  serialising dependency between kernels
- 4.8 Conclusion

**Status: [E]** for §4.2–4.4 (the work exists and is validated; it needs writing up).
**[M]** for §4.5–4.7 — RAPL energy and a profile equivalent to [MK]'s VTune run have not been
collected against the current binary.

> §4.7 is where the thesis earns RQ1: [MK]'s four benchmarks were independent, so his generality
> claim is "the same transforms worked four times." Here the four kernels are serially dependent
> within an iteration, which is *harder*, and the argument has to say so explicitly.

---

## 5. Algorithmic transformations on Programmable Logic (~40 pp)

[MK] Ch5 is his largest chapter (44 pp) and the methodological spine. Keep his three-category
taxonomy; the third category changes.

- 5.1 Experimental setup — VCK5000, Vitis 2022.2, and the emulation-vs-hardware caveat
- 5.2 General considerations when porting to PL via HLS
  - 5.2.1 Static memory and the dataflow canonical rules
  - 5.2.2 **The residency decision**: 1024×1024 matrices are DDR-resident and streamed in row
    tiles; on-chip memory holds only working tiles
- 5.3 **Top-down: dataflow structure of the placement iteration**
  - 5.3.1 Decomposing the iteration into stages
  - 5.3.2 Stream formats and the 128-bit logical word
  - 5.3.3 Where the cycle closes
- 5.4 **Bottom-up: intra-stage optimisation, per kernel**
  - 5.4.1 HPWL gradient — scatter-accumulate and the net-grouping problem
  - 5.4.2 Density — bin scatter, spectral solve, force gather
  - 5.4.3 Iteration update — Nesterov/BB
  - 5.4.4 Parameter scheduler and the BB norm reduction
- 5.5 **Verification against the golden, module by module** *(replaces nothing in [MK] — he had
  no quality axis; this is where the golden earns its keep)*
- 5.6 Single-kernel results per module: II, latency, resource, power
- 5.7 **Scaling out**: replication and parallelism across the die *([MK]'s third category was
  multi-kernel replication; here the analogue is replicating the spectral-solve lanes and the
  gradient pipeline, bounded by DDR bandwidth rather than by area)*
- 5.8 Analysis: resource utilisation and power draw across modules
- 5.9 Conclusion

**Status: [P]** — modules written and individually sw_emu-verified against the golden; Gate 1
HLS C-synthesis passed. `top.cpp` is still a bring-up scaffold with a `mode` switch; the unified
per-iteration datapath (Stage 5) is not composed. §5.6–5.8 are **[M]**.

---

## 6. Numeric optimisation (~24 pp)

[MK] Ch6, reframed. His error metric was deviation from a golden price; here it is degradation
of the objective, which is a stronger and more interesting question.

- 6.1 Experimental setup
- 6.2 **Precision and solution quality**
  - 6.2.1 Where precision bites: the spectral solve, gradient accumulation, coordinate update
  - 6.2.2 Effect on the convergence trajectory (HPWL and overflow vs iteration), not just on a
    final scalar
  - 6.2.3 Failure modes: does reduced precision stall convergence, or just shift the endpoint?
- 6.3 Representations: float32 / bfloat16 / fixed-point on PL; the AIE's native types
- 6.4 Resource utilisation and power draw across representations
- 6.5 Runtime and energy
- 6.6 Conclusion

**Status: [B]** — nothing exists. Entirely to be built and measured. Note [MK] §9.1's own
limitation: his microbenchmark suite's low resource utilisation meant it could not exercise peak
power. Here the real kernels *are* the benchmark, so that limitation does not apply — an
advantage worth stating.

---

## 7. Device residency: removing the host from the iteration loop (~20 pp)

[MK] Ch7's slot (a framework contribution around host↔device data movement), but a stronger
claim: he overlaps the transfer, this removes it.

- 7.1 Experimental setup
- 7.2 Related work — host-device streaming, including [MK]'s own framework
- 7.3 **Motivation: the measurement.** ~12 kernel launches/iteration (~0.9 ms of launch
  overhead) against ~76 MB/iteration of DMA (~7.6 ms) — roughly 8× more time in transfers than
  in launches, and every transferred byte is intermediate field data that never needed to leave
  device DDR.
- 7.4 Methodology
  - 7.4.1 Resident control: the schedule and convergence test on the PL
  - 7.4.2 Resident state: what fits on-chip, what stays in DDR
  - 7.4.3 The host boundary: upload once, download once
- 7.5 Evaluation: launches, bytes moved, runtime, energy
- 7.6 Conclusion

**Status: [P]** — `param_scheduler` and `bb_reduce` are built and verified bit-for-bit against
the golden trace, and C-synthesize clean, but are **not wired into `top.cpp`**. Composing the
resident loop is the single largest remaining build task in the thesis. §7.5 is **[M]**.

---

## 8. Integrating PL with AI Engines for a cyclic algorithm (~22 pp)

[MK] Ch8's slot, but this is the chapter the thesis exists for. His conclusion — that AIEs do
not support cyclic graphs, that PL loopback adaptors add functional support but stall, and that
the fix would be to move the logic wholesale onto the AIEs — is the problem statement here, and
the answer taken is different from the one he proposed.

- 8.1 Experimental setup
- 8.2 **The cyclic-graph problem.** Why a placement iteration is not expressible as an ADF graph,
  and why loopback-per-iteration is the wrong shape.
- 8.3 **Partitioning**: which kernels belong on the AIEs
  - 8.3.1 The criterion — acyclic, arithmetic-dense, streamable
  - 8.3.2 What stays on PL: the cycle, the control, the state
- 8.4 The AIE spectral-transform graph (one forward-FFT configuration; DCT/IDCT/IDXST pre- and
  post-processing in PL)
- 8.5 The AIE wirelength-gradient graph
- 8.6 Evaluation: CPU vs PL-only vs PL+AIE — runtime, energy, and placement quality
- 8.7 Conclusion

**Status: [P]** — the transform-mode FSM and IDXST are implemented in PL; the AIE FFT pool
streaming per iteration is not yet integrated end-to-end. §8.6 is **[M]** and needs real
hardware.

---

## 9. Conclusion (~6 pp)

- Restate RQ1–4 as headings and answer each ([MK]'s device, worth copying exactly)
- 9.1 Limitations
- 9.2 Future work

---

## Page budget

| Ch | Topic | Target pp |
|---|---|---|
| 1 | Introduction | 6 |
| 2 | Reconfigurable architectures | 22 |
| 3 | VLSI global placement | 18 |
| 4 | Software golden + CPU baseline | 28 |
| 5 | Transformations on PL | 40 |
| 6 | Numeric optimisation | 24 |
| 7 | Device residency | 20 |
| 8 | PL + AIE integration | 22 |
| 9 | Conclusion | 6 |
| A–C | Appendices | ~30 |

Body ≈ 186 pp, matching [MK]'s 188. Appendices absorb per-module HLS listings and full result
tables so Chs 5–6 stay readable — [MK] put 36 pp there and it visibly helped.

---

## Open risks

1. **Real-hardware access.** [MK] ran 5-run averages on a dedicated testbed. Local access here is
   `sw_emu` only; `TARGET=hw` runs on a colleague's card. Chapters 5–8 all carry **[M]** items
   that require real runs. This is the top scheduling risk and should drive the plan, not be
   discovered late.
2. **VCK5000 power measurement.** [MK] §9.1: *"The evaluation ... on the Versal ACAP lacks power
   draw and energy results, as these measurements are not available in the experimental setup."*
   Same card. With energy now a first-class axis, this has to be solved before Chs 5–8 results
   are collected, or the energy claims collapse into a limitation.
3. **Ch6 is greenfield.** The precision study does not exist in any form. It is also the most
   separable chapter — a candidate to cut or shrink if the schedule tightens.
4. **Chapter ordering.** The outline mirrors [MK] (numerics before residency before AIE). An
   alternative is 5 → 7 (residency) → 8 (AIE) → 6 (numerics), since residency follows directly
   from the Ch5 dataflow and numerics is orthogonal to both. Deferred, not decided.
