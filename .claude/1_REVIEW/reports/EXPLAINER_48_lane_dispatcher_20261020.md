# EXPLAINER #48: the 8-lane deposit and its dispatcher — slide-deck brief

*2026-10-20. One section per slide: what it shows, then what to say. Numbers from
[[PLAN_48_bin_density_lanes_20261020.md]] and the lane model (`host/sw_flat/model/lane_model.cpp`).*

---

## Slide 1 — The problem

**Shows:** one deposit unit, cells entering one per clock, a counter: bigblue4 ≈ 5.5 M slots ≈ 21 ms at
300 MHz. Beside it, XPlace's whole scatter on a 3090: ~4 ms.

**Say:** today's `bin_density_CU` adds one cell's footprint into the grid every cycle. That is correct and
bit-exact, but one cell per cycle is 5× too slow for the largest designs. We need several cells per cycle.

---

## Slide 2 — What actually limits us: the memory, not the math

**Shows:** a cell's footprint over a 3 × 3 patch of bins; 9 arrows into 9 memory banks; each bank labelled
"one read-add-write per cycle".

**Say:** computing how much a cell deposits in each bin is cheap and pipelines easily. The scarce thing is
the grid memory: each bank can update one bin per cycle. A second cell in the same cycle needs its *own*
banks. So more throughput means more banks working at the same time, on different cells.

---

## Slide 3 — The idea: give each lane its own rows

**Shows:** the 1024 × 1024 grid with rows coloured by owner, 8 colours. Each colour = one lane = its own 16
banks covering only those rows.

**Say:** split the grid by rows among 8 lanes. Each lane is a copy of today's deposit unit (16 banks,
forwarding) that owns only its rows. Eight lanes can each deposit a cell in the same cycle, because they
touch different memories. At 1024 with 8 equal shares, each lane's banks are exactly one URAM each: still
128 URAMs in all. More lanes would not be free.

---

## Slide 4 — Some cells belong to two lanes

**Shows:** a footprint crossing the boundary between two owners' rows; the top rows go to lane A, the
bottom rows to lane B.

**Say:** a footprint covers up to 4 rows, so near a boundary it touches two owners. It is then sent to both;
each adds only its own rows. Nothing is approximated: the two halves together are exactly the one deposit.
The cost is that both lanes spend a cycle on it.

---

## Slide 5 — The dispatcher's job, in one picture

**Shows:** DDR → beat of 16 slots (x, y, footprint index) → "which rows?" → arrows into 8 lane queues → 8
lanes → 8 groups of banks.

**Say:** the dispatcher is the traffic cop in front of the lanes. Every cycle it can read one beat of 16
slots. For each slot it works out which rows the footprint covers, which lane(s) own those rows, and puts
the slot in that lane's queue. It does no depositing itself.

---

## Slide 6 — "Which rows?" without dividers

> **Superseded 2026-10-22:** synthesis of one producer (`pl/synth/bin_density_producer`) priced the 4
> divides at about 4 multiplies (91 vs 95 DSP, 17.2 K vs 17.1 K LUT; +13 pipeline stages). The DSPs go to
> the 16 amounts' ~32 multiplies. Keep the divides: bit-exact with today's golden, no model change. The
> slide below is kept as the record. Meow.

**Shows:** the footprint drawing (cell, stretched footprint, `half_h`, `half_ch`), then two lines:
`yl = y + half_h − half_ch`, `yh = yl + 2·half_ch`, and `row = ⌊ yl / bin_h ⌋` crossed out, replaced by
`row = ⌊ yl · inv_bin_h ⌋`.

**Say:** the bottom of a footprint is the cell's centre minus half the stretched height; the top is that
plus the full stretched height. Dividing by the bin height gives the row. A float divider is large, and the
dispatcher would need 16 of them. A multiply by the precomputed `1 / bin_h` is cheap. But a multiply does
not round exactly like the divide, so the device and the software golden would disagree right at bin edges.
The fix is to change the golden to the multiply too (in sw_flat first, measuring the drift), so the device
can copy it exactly. That also removes the divides inside the lanes.

---

## Slide 7 — Why each lane needs a queue

**Shows:** two beats. Beat 1 gives lane 3 six slots and lane 5 none; beat 2 the reverse. Without queues the
dispatcher waits on lane 3 while lane 5 idles; with queues both keep busy.

**Say:** a beat rarely splits evenly over the lanes. Without a queue, the dispatcher must wait until the
busiest lane has taken its share before reading the next beat, and the other lanes sit idle (2–3× overall).
A queue per lane lets the dispatcher move on; a lane that drew a lot catches up while others take the next
beats. The lanes only wait for each other when a queue fills.

---

## Slide 8 — Read the stream from 4 places

**Shows:** the slot stream as a long bar; one reader walking it vs four readers spaced along it, taking
turns.

**Say:** slots that are next to each other in memory are next to each other on the die, so a run of beats
all goes to the same lane and its queue fills. Reading from four places in the stream at once mixes
distant parts of the die into consecutive beats. This helped more than making the queues deeper.

---

## Slide 9 — Two cells per cycle in one lane

**Shows:** a lane's 16 banks as a 4 × 4 grid of squares. Cell 1 lights up 6 squares; cell 2 lights up 4
different squares; both go in the same cycle. A third cell overlapping cell 1's squares waits.

**Say:** a cell uses only 4–9 of its lane's 16 banks, so the rest sit idle each cycle. If the next cell in
the queue needs only banks the current one does not use, the lane deposits both at once. Each bank still
does one update per cycle, so forwarding is unchanged. Straddler halves are the best case: a half uses
only one or two bank rows. (Lane model: on fft_a this lifts 4-row strips from 5.0× to 7.0×.)

---

## Slide 10 — Which rows does each lane own?

**Shows:** five row maps side by side (bands, 4-row strips, 8-row strips, graded bands, middle-striped),
each with URAMs and whole-run speedup.

**Say:** the choice trades two problems. Contiguous bands rarely split a cell, but the early pile-up at the
centre lands on one or two lanes. Thin strips spread any cluster over all lanes but split more cells.
Graded bands (thinner in the middle) and a striped middle were tried: they did not beat 4-row strips, and
graded bands cost 192 URAMs. Whole run, with 4 read points, 128-deep queues and pairing:

| map | URAMs | mgc_fft_a | adaptec1 | mms/newblue4 | superblue12 | bigblue4 |
|---|---|---|---|---|---|---|
| bands | 128 | 6.15 | 3.43 | 2.39 | 2.74 | 4.59 |
| **strip4** | 128 | **7.02** (4.3) | **5.72** (2.2) | **4.79** (1.6) | **4.58** (1.6) | **7.16** (3.2) |
| strip8 | 128 | 5.67 | 5.23 | 4.39 | 4.27 | 6.66 |
| graded (thin middle) | 192 | 5.38 | 3.47 | 2.54 | 2.66 | 4.16 |
| mid-strip | 128 | 6.57 | 4.13 | 3.35 | 3.40 | 5.40 |

(In brackets: the first 16 iterations alone, when the cells are still piled in a few rows. Nothing spreads
that pile, because it is shorter than any lane's share of rows.)

---

## Slide 11 — The overflow barrier

**Shows:** timeline: cells → (lanes drain) → overflow reduce → fillers → drain to DDR.

**Say:** the overflow must be read after the cells and before the fillers, so the lanes empty their queues
once between the two. It costs one queue-drain per evaluation, a few hundred cycles.

---

## Slide 12 — What we still decide

**Say:** (1) the row map; (2) the golden's row index by multiply (a model change, measured first);
(3) pairing depth (look one item ahead, or more); (4) the load/drain format (4 × 4 tiles per beat),
parked for later.
