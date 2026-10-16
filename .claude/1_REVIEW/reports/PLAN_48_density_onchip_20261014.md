# #48 — On-chip density path: plan outline (2026-10-14)

**Goal:** a VCK5000 placement step faster than XPlace on a GPU for large designs, by keeping the
density grid on chip and running the density path (scatter → FFT → gather) alongside the wirelength
gradient. The bar is the paper's 3090 numbers: bigblue4 **10.0 ms/step** non-deterministic (Table
IV), about **12 ms** deterministic (14.26 s GP, Table II), not our 3080.

**Why this path:** on the GPU, bigblue4's density scatter and force gather are **54%** of kernel time
and the DCTs **3.6%** ([[_NEW_REPORT_xplace_gpu_iteration_breakdown_20261013.md]]). Fixing
scatter/gather is necessary, not sufficient: the whole step has to fit.

**Scope:** v1 targets the **1024 grid**. 2048 (needed for modern designs, 7 of our 44 use it) is a
planned extension by row-band tiling. bigblue4 at 1024 vs 2048: +0.16% final HPWL (one seed,
`vck5000/results/DSE_20261013_130904/`).

Phases 1 and 2 can run in parallel; 2b needs 1's allocation; 3 needs 2's interface frozen.
Items marked *(added)* were not in Mark's list of four.

---

## 1. URAM budget

Decide what lives on chip, when, and at what precision, before designing around a guess.

- [ ] **Inventory.** Device: 463 URAM288 (4K × 72b; a 1M-float array = 128), 967 RAMB36. Measure what
      `hpwl_gradient_CU` uses today from its synthesis report (128 per array at 1M slots, from #41).
- [ ] **Array list with sizes at 512 / 1024**, fp32 vs narrower:
      rho accumulator (int32), Ex, Ey (fp32 or fp16), the overflow map without fillers, footprint
      table, FFT corner-turn buffers (Geert), gradient pos/grad per axis, chunk tail buffers.
- [ ] **Liveness table:** which arrays are live in each phase of one step (scatter, FFT, gather, WL
      gradient, update). Arrays whose live ranges don't overlap can share memory, as `pos_URAM` is
      already reused as the macro-fold accumulator.
- [ ] **Precision study (software):** quantize Ex/Ey to fp16 and rho to the chosen fixed-point scale
      in sw_flat, then run `dse.py` on a few designs. Accept only if final HPWL is within noise.
- [ ] **Scenarios:** (A) density resident, gradient chunked; (B) one gradient axis resident and the
      field in fp16; (C) everything at 512 only. Pick one with ≥10% headroom.

**Done:** a chosen allocation, confirmed by synthesizing a stub top that declares every array
(utilization report, not arithmetic). **Falsified if** no scenario fits 1024 with headroom. Then the
density path streams the field from DDR, and the gather design changes.

## 2. Bin density compute unit (`density_map_CU`)

- [x] **Skeleton (2026-10-13, `2dad68f`):** contract, plain loop, tier-1 harness vs a double golden
      (rel_rms 2.9e-6 at 1024). [[_NEW_EXPLAINER_density_map_CU_brief_20261013.md]]
- [ ] **Real-design harness** vs sw_flat `Placer::bin_overlap`, and count the footprint table on all
      44 designs (4096 is assumed; MMS movable macros are the risk).
- [ ] **Overflow map decision:** the stop test needs the map without fillers, every step (XPlace
      computes cells and fillers separately and adds). A second accumulator, or fillers last with a
      snapshot of the map before them.
- [ ] **Fixed-point accumulator:** scale and width from the densest bin on all designs; tolerance
      stated in the harness.
- [ ] **4×4 banked grid:** a standard cell's ≤3×3 window written in one cycle; II=1 per cell.
- [ ] **Macro slow path:** footprints larger than the window.
- [ ] **Synthesis (tier 2):** II, URAM count, timing at 3.33 ns.
- [ ] *(added)* **Density force gather CU:** the mirror of the scatter (read-only, same footprint and
      banking), ported from `vck5000/.../force_gather.hpp` into `pl/`. The other half of the 54%.

**Done:** scatter and gather each at one cell per cycle in synthesis, matching sw_flat on real
designs within the stated tolerance.

## 2b. Dataflow optimization

- [ ] **Cycle model per step** (extend `host/sw_flat/model/`): passes, cells per pass, cycles per
      lane count, against the 10–12 ms bigblue4 bar. Today's estimate: one cell per cycle is ~7–10
      ms per pass, so **≥4 cells per cycle** are needed.
- [ ] **Lanes:** private grid copies at 512; row-band ownership at 1024. Measure band load balance
      from recorded position traces (early steps are clustered at the centre).
- [ ] **Pass fusion:** gather into the combine pass (with the Barzilai-Borwein norms), scatter into
      the update pass. The update can't join the combine pass: the step size needs global sums first.
- [ ] **DDR controller assignment:** positions, footprint indices, pin records and rho across the 4
      controllers, so concurrent streams add bandwidth rather than split it.
- [ ] **Concurrency:** WL gradient and density path running at the same time; the step costs about
      max(WL, density) + update.

**Done:** co-simulated cycle counts that match the model, and a per-step projection under the bar
for bigblue4.

## 3. Integration with Geert's AIE FFT

- [ ] **Interface agreement:** rho format (float vs fixed, column-major, beat width), PLIO streams
      vs DDR, who owns the transposes, and Ex/Ey return order (row by row, which suits 2048 bands).
      Confirm the DCT pre/post-processing split (CLAUDE.md: PL does it, AIE only the FFT).
- [ ] **Joint golden:** sw_flat `compute_electric_fields` end to end (rho → Ex, Ey), tolerance stated.
- [ ] **Wire into `top.cpp`** and verify in sw_emu (the only emulation that works with AIE here).
- [ ] **Real hardware** on Geert's card.

**Done:** rho in, Ex/Ey out on hardware, matching the golden.

## 4. *(added)* End-to-end evaluation

- [ ] Full step on the device, trajectory vs sw_flat (the #45 path).
- [ ] Per-step time vs the paper's 3090 numbers on the ISPD2005 suite; small designs too, where
      the GPU is launch-bound (adaptec1: kernels busy 46% of the step).
- [ ] **Energy per placement:** board power on the VCK, `nvidia-smi` power on the GPU.
- [ ] **2048 extension:** row-band tiling, band queues per DDR controller, results returned to slot
      order (designed in conversation 2026-10-13; write up when started).

## Open decisions

1. Which of the gradient and the density path stays resident at 1024 (phase 1 decides).
2. fp16 field storage (phase 1 precision study).
3. Overflow map: second accumulator or snapshot (phase 2).
4. Rho / field interface (phase 3, with Geert).
