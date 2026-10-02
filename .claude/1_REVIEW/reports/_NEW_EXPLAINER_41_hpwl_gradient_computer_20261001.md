# EXPLAINER #41 — `hpwl_gradient_computer`: slide-deck source (2026-10-01)

Source material for a slide deck on the WA-wirelength gradient module. One `##` section per slide:
a headline, the bullets for the slide, and a suggested visual. Numbers are the latest measured
(tier-1 2026-09-22, RTL co-sim and place-and-route 2026-09-23); each carries its source.

Code: `vck5000/bring_up/hpwl_gradient_computer/src/hpwl_gradient_computer_top.cpp` (kernel top) →
`src/modules/hpwl_gradient_computer.hpp` (the module). Evidence:
[[_NEW_REPORT_41_record_datapath_20260922.md]], [[_NEW_REPORT_41_beat_packer_20260922.md]],
[[_NEW_HANDOFF_41_pnr_timing_20260923.md]].

---

## 1. Title
**`hpwl_gradient_computer`: the ePlace wirelength gradient in one pass over a static DDR stream**
- PL kernel for the AMD Versal VCK5000, part of the AIEplace PL-centric placer (`pl_algo`)
- Computes the weighted-average (WA) wirelength gradient for every movable node, one axis per call
- Per-net HPWL comes out as a free by-product

## 2. What it computes
**The gradient of the smoothed wirelength, per node**
- ePlace replaces each net's HPWL with a WA (weighted-average) approximation with smoothing γ
- For each pin on a net, using exponents shifted by the net's bounding box (so every term is in [0, 1]):
  - a⁺ = exp(−(max − x)/γ), a⁻ = exp(−(x − min)/γ)
  - B± = Σ a±, C± = Σ a±·x over the net's pins
  - g = ((1 + x/γ)·B⁺ − C⁺/γ)·a⁺/B⁺² − ((1 − x/γ)·B⁻ + C⁻/γ)·a⁻/B⁻²   (Dhar eq. 4)
- A node's gradient = sum of its pins' g; a macro's gradient = sum over its pins
- Same maths as the CPU golden `computeHpwlPartials_CPU` (sw_only `Partials.cpp`), which tracks XPlace
- *Visual:* one net, its bbox, two pins with a⁺/a⁻ drawn as distances to max/min

## 3. Why the architecture looks like this
**Random access belongs on chip; DDR should only stream**
- The gradient engine consumes 16 pins/cycle → **4.8 G pins/s per axis** at 300 MHz
- Gathering positions and scattering gradients through DDR falls **10–50× short** of that
- So: node positions and gradients live in **banked on-chip URAM** (32 banks × 2 arrays, 1 M slots each)
- DDR carries only a **static, sequential pin-record stream**, the same every iteration
- The host-side packer orders records so the on-chip random accesses never collide
- Lineage: Dhar et al., FPL 2019, Method 1 (tree structure, Figs. 5–8); extends `hpwl_computer_v2` (#40)
- *Visual:* "DDR: sequential only" vs "URAM: random access" split

## 4. The pin-record stream
**One 32-bit record per pin, 16 pins per 512-bit beat**
- `record = node_slot << offset_bits | offset_idx`: which node, and which pin offset on it
- A beat holds nets of a single degree (2..16 pins), so 16/degree nets per beat; `beat_count[]` gives the degree boundaries
- Flags cost no bits: EMPTY is the all-ones record; "fixed" and "repeated node" are comparisons on `node_slot`
- All 44 benchmark designs encode in ≤29 bits with ≤131 offset-table entries
- Packing efficiency: with 32 banks plus same-node lane merging, **≥99.69% of the ideal beat count on all 44 designs**
- *Visual:* one beat as 16 lanes, colored by net, with one repeated node highlighted

## 5. Kernel interface
**Seven m_axi bundles, so streams never share a port**
| bundle | port | direction | contents |
|---|---|---|---|
| gmem0 | `records` | in | pin-record stream, this axis |
| gmem1 | `beat_count` | in | beats per net degree |
| gmem2 | `pos` | in | slot-major node positions |
| gmem3 | `macro_pins` | in | macro-pin list (refresh, then fold) |
| gmem4 | `offset_table`, `exp_lut` | in | pin offsets; exp(−t) table (read one after the other) |
| gmem5 | `out_beats` | out | per-net HPWL |
| gmem6 | `grad` | out | gradient, movable slots |
- Scalars (`inv_gamma`, `inv_lut_step`, `offset_bits`, `first_fixed_slot`, sizes) go on the AXI-Lite control port
- *Visual:* kernel box with the seven ports around it

## 6. Execution: nine phases
**Load on chip, one streaming pass, fold, drain**
1. `cache_counts`: beat counts into registers
2. `load_slot_array`: positions into `pos_URAM`, 16 slots/cycle
3. `refresh_macro_pins`: macro-pin slots = macro position + offset
4. `load_offset_table`: 16 lane copies in BRAM
5. `load_exp_lut`: 32 copies of (lut[i], lut[i+1]) pairs, so each interpolated lookup is one read
6. `fill_slot_array`: zero `grad_URAM`
7. **`gradient_beat_loop`**: the main loop, one beat per cycle (next slide)
8. `fold_macro_pins`: each macro's gradient += its pins' gradients
9. `drain_slot_array`: `grad_URAM` to DDR, 16 slots per beat
- Every loop is pipelined at **II=1**
- *Visual:* the phase-sequence diagram (DDR | phase | on-chip buffer, one row per phase)

## 7. The beat loop
**One 16-pin beat per cycle, nine stages, pipeline depth 72**
1. Decode 16 records
2. Gather pin positions (bank-major read of `pos_URAM` + pin offset)
3. Bbox trees: per-net max and min, for every possible degree at once
4. Exp terms: a⁺, a⁻ by LUT with linear interpolation
5. Four sum trees (`dhar_tree<AddOp>`): B⁺, C⁺, B⁻, C⁻ per net
6. One 1/B² per net, not per pin
7. Combiner: eq. 4 per pin
8. Merge: a 4-step segmented scan sums a node's repeated pins into one lane
9. Scatter-add: each of 32 banks does at most one read-add-write into `grad_URAM`
- Side output: HPWL = max − min per net → `out_beats`
- *Visual:* the beat-loop pipeline diagram (vertical stages, inputs left, outputs right)

## 8. The hazard contract
**Read-add-write every cycle, made safe by the host's schedule**
- The scatter-add reads, adds, and writes URAM in a pipeline: back-to-back updates to one node would race
- The host packer keeps any node's updates **≥4 beats apart** (`HAZARD_DISTANCE = 4`)
- That distance is declared to HLS as a true dependence: HLS must fit the round trip within it, or raise II
- The schedule and the pragma are one contract; the macro fold uses the same contract
- **RTL co-simulation: spacing 4 passes; 3, 2, 1 corrupt the gradient**, while C simulation passes all of them, so co-sim is the real gate
- *Visual:* timeline of a node's updates with the 4-beat window shaded

## 9. Verification
**Checked against independent goldens at every tier**
| tier | check | result |
|---|---|---|
| 1, offline | gradient vs golden from the parsed netlist | rel_rms 3.9e-7 (tolerance 1e-5); HPWL bit-exact |
| 1, mutation | deliberately broken variants | 10/11 caught (1 is equivalent) |
| 1, real designs | adaptec1, mgc_fft_1, MMS newblue2 | gradient rel_rms 3–8e-7 |
| 2, C-synthesis | full 1 M-slot capacity | every loop II=1 |
| 3, RTL co-sim | hazard spacing sweep | passes at 4, fails at 3/2/1 |
- Caveat: newblue2's worst-case relative error is 9.5e-5 against a 1e-4 bound, from float cancellation in the absolute-coordinate combiner on macro-heavy nets (known issue; the fix is net-local coordinates)

## 10. Implementation results
**Place-and-route on the xcvc1902 (VCK5000), 3.33 ns target**
| LUT | FF | DSP | BRAM | URAM | post-route period |
|---|---|---|---|---|---|
| 110 K | 125 K | 596 | 220 | 256 of 463 | 4.335 ns (≈231 MHz) |
- **Timing not yet met** (worst slack −1.0 ns)
- The top failing paths start at one float adder that HLS shared between the macro refresh and the macro fold; its output fans out into all 64 URAM banks, and 64% of the delay is routing
- That adder is outside the beat loop: candidate fix is to unshare it and register before the broadcast
- At 231 MHz as routed: 3.7 G pins/s per axis (target 4.8 G at 300 MHz)
- URAM packs efficiently: 128 URAMs per 1 M-slot array (4.6 MB physical for 4 MB of data)

## 11. Limits and next steps
- **Timing closure**: diagnose the remaining ~20 K failing endpoints; if deeper pipelines push the read-add-write past 4 beats, raise `HAZARD_DISTANCE` (8 costs <0.5% bubbles) and re-run the co-sim sweep
- **Nets of 17–100 pins are dropped today**: 20–29% of ISPD2005 pins, +12.4% HPWL. Proposal: three exact II=1 passes on the same engine, about 2× gradient cycles on ISPD2005 (awaiting Mark's decision). Nets >100 pins are masked, as XPlace does
- **Designs over 1 M slots** use the chunked sibling `hpwl_gradient_computer_v2` (8 of 44 designs, 4–19% ghost overhead)
- Not yet wired into `pl_algo`'s `top.cpp`; it is a bring-up module

## Takeaway (closing slide)
- Turning DDR random access into a **static sequential stream plus on-chip banked memory** is what lets the gradient run at one beat per cycle
- Correctness of the on-chip read-add-write comes from a **host-schedule/HLS-pragma contract**, proven in RTL co-simulation
- Numerically verified to ~4e-7; the remaining work is timing closure and large-net support
