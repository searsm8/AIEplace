# REPORT #45 step 5b (part 2): the rest of the PL's arithmetic in `sw_flat_place`

*2026-10-16. Branch `pl_algo`. Follows [[REPORT_45_slot_order_sums_20261014.md]] (part 1: slot-order
sums and the exp table). Closes step 5b.*

## Result
`sw_flat_place` can now compute the WA gradient and the HPWL **exactly as `hpwl_gradient_CU` does**,
bit for bit. Three new `[sw_flat]` switches, all off by default (`make check-placer` still
bit-identical to sw_only):

| key | effect | PL source |
|---|---|---|
| `wa_sums = "tree"` | per-net B+, C+, B−, C− summed over the record lanes by the Dhar trees; a large net's beats through the window tree, newest first | `wa_sums.hpp`, `dhar_tree.hpp`, `record_gather.hpp balanced_tree` |
| `grad_merge = "stream"` | per beat, a node's repeated lanes merged by the Hillis-Steele scan, then scatter-added in stream order; macro pins folded in list order; tails merged into owners, borrowers ascending | `wa_gradient.hpp`, `slot_arrays.hpp fold_macro_pins`, `hpwl_gradient_CU.hpp merge_tails_pass` |
| `hpwl = "fixed"` | the schedule's HPWL = int64 sum of `hpwl_to_fixed(max − min)` per net and axis, over the stream's nets | `hpwl_CU.hpp` |

With these plus `wa_exp = "lut"`, the gradient of every owned slot and both int64 HPWLs equal the CU's
**bit for bit on 6/6 designs** (below). Together with part 1's `slot_order` and #48's B', this is
the golden for step 6's modules and step 7's composed run.

**The combiner was already the CU's.** `wa_gradient.hpp`'s per-pin expression is sw_only's
`computeHpwlPartials_CPU` term for term (same association, same `1/B²`), so there was no "formula"
switch to add. What differs is the *order*: in which lane order a net's terms are summed, and in which
order a node's partials reach its gradient.

## What changed (`host/sw_flat/`)
- `placer/record_stream.cpp` (new): decodes the encoded design once at setup into a lane table (per
  stream position: degree, and per lane its FlatDesign pin, global slot, and whether it takes a
  gradient), the fold list, the tail-merge list and the stream's net set; then the three switches.
  Setup cross-checks every lane's slot against its pin's node and exits on a mismatch.
- `placer/wirelength.cpp`: `compute_hpwl_partials` now dispatches. The per-net loop moved unchanged
  into `compute_net_partials`.
- `Placer` keeps the `packer::Chunked` it was laid out from (it used to be dropped after setup).
- The switches require `ignore_net_degree = 96` (the packer's mask) and exit otherwise. Any other value
  would compare two net sets, not two arithmetics.
- The Dhar and window trees are **included from `pl/src`, not retyped**: they *are* the summation
  order. `pl/test` holds their sums against a double-precision golden. So the check below proves that
  the stream walk, the combiner, the merges, the folds and the HPWL agree with the CU. It does not
  independently re-derive the tree shapes.
- The float conversion of the HPWL, `(float)ldexp(double(x + y), −24)`, is **our choice**: the device
  returns the two int64s, and step 7's host has to do the same.

## Verification
`make -C host/sw_flat check-record-stream` (`test/record_stream_check.{cpp,sh}`, ~1.5 min): per design,
the D6 config with all four switches on, 30 iterations in, then on the same probe positions:
`sw_flat`'s gradient vs `plalgo::hpwl_gradient_CU` compiled with g++ (tier-1 style, `tier1_stub.hpp`;
the CU's gradient image starts NaN).

| design | chunks | merged lanes | folds | tail merges | [1] gradient | [2] HPWL x, y | [3] control (sw_only arithmetic) |
|---|---|---|---|---|---|---|---|
| mgc_fft_a | 1 | 0 | 0 | 0 | 0 differ | equal | 42,142 slot-axes differ |
| mgc_pci_bridge32_b | 1 | 32 | 0 | 0 | 0 | equal | 35,067 |
| adaptec1 | 1 | 18,359 | 0 | 0 | 0 | equal | 345,890 |
| mms/adaptec1 | 1 | 18,359 | 16,265 | 0 | 0 | equal | 346,607 |
| bigblue4 | 3 | 141,740 | 0 | 266,623 | 0 | equal | 3,326,778 |
| mms/bigblue4 | 3 | 142,527 | 62,662 | 277,961 | 0 | equal | 3,312,341 |

([1] counts OWNED slots × 2 axes differing in any bit; mms/adaptec1 has 210,967 owned slots, bigblue4
2,169,183. [3] is the
same positions with serial sums and file-order merge: it must differ, or [1] proves nothing.)

**Which half matters ([info] rows):** each [info] row turns on one switch, so its residual against the CU
is due to the *other* one. At iteration 30, with tree sums on and file-order merge, 4–12% of slot-axes
differ: that is the **merge order**, which matters only for nodes with three or more partials (two adds
commute). With serial sums and the stream merge, 68–82% differ: that is the **sum order inside a net**,
which changes every partial of any net with three or more pins. The large relative
numbers are slots whose partials cancel to almost nothing, not large errors.

**Mutants** (`.claude/2_ARTIFACTS/scratch_45/record_stream_mutants.sh`): 7 deliberate breaks, each
caught on every design that has the feature: window oldest-first, no merge scan (survives only on
fft_a, which has 0 merged lanes), no tail merge, tail merge borrowers descending, no macro fold,
serial small-net sums, HPWL y dropped.

## Measurement: drift vs the bit-identical default
`test/sum_order_drift.py`, as in part 1: D6 config, 2 threads per run, final masked HPWL vs base, against
the 2-ULP `init_gamma` noise band. The noise bands came out **identical to part 1's**, as they should:
same seeds, same arithmetic. `pl` = slot order + exp table + all three switches. `pl+Bprime` adds #48's
B' (step 6's golden config). Full table: `.claude/2_ARTIFACTS/scratch_45/sum_order_drift_record_stream_20261016.md`.

| design | noise band | tree | stream | fixed | pl | pl+Bprime |
|---|---|---|---|---|---|---|
| mgc_fft_a | −0.473..+0.056% | +0.036% | −0.433% | −0.053% | −0.456% | −0.456% |
| mgc_pci_bridge32_b | −0.501..+1.854% | +0.757% | +0.437% | −0.243% | −0.105% | −0.105% |
| adaptec1 | −0.018..+0.053% | +0.009% | +0.031% | +0.006% | +0.023% | +0.006% |
| mms/adaptec1 | −0.025..+0.022% | **+0.038%** | −0.005% | +0.003% | **−0.490%** | **−0.490%** |
| bigblue4 | −0.013..+0.000% | −0.007% | −0.010% | **+0.007%** | **+0.012%** | **+0.007%** |

- Every run converged, within ±3 iterations of base. 19 of 25 fall inside the band.
- **bigblue4's 3 outliers are +0.007..+0.012%.** All six noise samples there happen to fall at or below
  0, so the band is too narrow to call these an effect.
- **mms/adaptec1's `pl` −0.49% is a selection flip, not drift.** Its trajectory stays within 1e-3 of base
  at every iteration checked (row 657: +3e-4). But the best-solution rule then picks the **primary**
  slot at iteration 641. Base and every noise run pick **aux** at the last iteration. Iteration 641 has
  lower HPWL and higher overflow (exact overflow 0.076 → 0.089, the +18%). fft_a's `pl` flips the same
  way (primary 601 vs aux 625) and still lands inside its wider band. This is the same mechanism as
  part 1's "the stop fires a few iterations off". It lives in the best-solution rule (#33's
  territory), not in the arithmetic.
- `pl` and `pl+Bprime` are identical on fft_a, pci_bridge32_b and mms/adaptec1, consistent with #48:
  B' equals sw_only's metric on most designs.

**So the PL's arithmetic hides no semantic difference either.** Step 6's golden is `pl+Bprime`.

## Found on the way: sw_only's float HPWL metric reads 0.4% low on bigblue4
> **Fixed 2026-10-16 by #49** (Mark un-froze sw_only for it): the metric now sums in double. Placement
> quality is unchanged; see [[REPORT_49_hpwl_double_sum_20261016.md]].

At the same iterate, the schedule's HPWL under `hpwl = "fixed"` was 0.46% above the float metric on
bigblue4, and within 1e-5 of it on the other four designs. `record_stream_check` now prints both
against a double sum (`[info]`, iteration 30):

| design | int64 vs double | sw_only's float vs double | sw_only's net set vs the stream's (double) |
|---|---|---|---|
| mgc_fft_a | −2.3e-8 | −3.3e-6 | 0 |
| adaptec1 | +1.8e-8 | +1.3e-6 | 0 |
| bigblue4 | −1.8e-8 | **−3.9e-3** | 0 |

The nets are the same, so the cause is **float accumulation**. `total_wirelength` (= sw_only's
`DataBase::computeTotalWirelength`) adds 2.2 M per-net HPWLs serially into one float. That float reaches
~7e8, where one ulp is 64 DBU, more than many nets measure. The int64 sum is exact to 2^-24 per net.
- **What it affects:** on the largest designs, sw_only's λ-schedule input, its best-solution choice, and
  the GP HPWL it prints. It does **not** affect the scored goldens: legal-vs-legal HPWL comes from
  XPlace's evaluator.
- **sw_only is frozen, so I changed nothing there.** The PL route sees the accurate number. Whether
  sw_only should also get a double or int64 metric is Mark's call, because it would un-freeze sw_only.

## Next
- Step 6: port the density path to `pl/` one module at a time, each checked against sw_flat in the
  `pl+Bprime` configuration.
- Still open for Mark (from part 1): the grid. sw_flat follows sw_only's per-design formula; the
  hardware grid is 1024.
