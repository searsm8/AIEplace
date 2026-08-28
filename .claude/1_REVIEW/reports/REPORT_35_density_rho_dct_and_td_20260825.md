# The density field ρ, the DCT electrostatic solve, and the target-density cap

*2026-08-25. Written for a reader who knows analytical / electrostatic global placement
(the ePlace / DREAMPlace family) but nothing about XPlace or AIEplace internals. It explains
how AIEplace builds its bin-density field ρ, why a 2-D DCT turns that field into the placement
force, and the one place AIEplace now deliberately diverges from XPlace: how a fixed macro's
occupancy is folded into ρ. This is also the basis for a slide deck — each `##` is roughly one
section of talk track.*

---

## 1. The model in one paragraph

Global placement minimizes total wirelength subject to a **density constraint**: cells must not
pile up on top of each other. ePlace makes that constraint tractable by casting it as
**electrostatics**. Treat every cell as a blob of positive electric charge; the amount of charge
in a region is its cell-area **density ρ**. Like charges repel, so a field pushes cells out of
crowded regions into empty ones — exactly the spreading a placer wants. Formally the optimizer
minimizes `WL(x) + λ · N(x)`, where `N` is an electrostatic potential energy whose **gradient is
the electric field**. So each global-placement iteration needs two gradients: the wirelength
gradient (cheap, local) and the **density gradient = the electric field E**, which is global —
every cell's position affects the field everywhere. Computing E efficiently is what the DCT is
for (§4). Building the charge density ρ that E is computed from is §2–§3.

Everything below is about **ρ and E**. Wirelength is out of scope.

---

## 2. Building ρ, part 1 — the bin grid and node footprints

### 2.1 Bins

The die is divided into an `M × N` grid of rectangular **bins**. For each bin,

```
ρ[bin] = (cell area deposited in the bin) / (bin area)
```

so `ρ = 1` means a bin is exactly full, `ρ = 0` empty. ρ is the discrete charge-density map the
whole solve runs on. In code the raw accumulator holds *deposited area* (`total_overlap`); ρ is
that divided by `bin_area`.

### 2.2 Footprints and the √2 inflation (smoothing)

Each node's area is scattered into the bins its rectangle overlaps. If a node is much smaller than
a bin, depositing it naively spikes a single bin — a delta function. The solver needs a **smooth**
ρ (its gradient is a force; a spiky ρ gives a jagged, useless force), so each footprint is first
**inflated to at least √2 bins in each dimension**, and given an **area-conserving weight**:

```
weight = real_area / inflated_area
```

The inflated rectangle spreads the same total area over ~one bin's worth of support, so the cell's
*mass* is preserved but *smeared* to grid resolution. This is the "smoothed density" — the field
the optimizer actually descends. (A second, exact ρ with sharp footprints is also computed for
*reporting* physical overflow; see §5.) A small in-die shift keeps every inflated footprint inside
the die boundary.

> **Terminology bridge for XPlace readers:** the √2 inflation is XPlace's `expand_ratio`; the
> area-conserving weight is `node_area / clamp_node_area`. Same idea, same purpose.

### 2.3 Two passes: fixed, then movable

ρ is built in a deliberate order:

1. **Pass 1 — fixed components** (macros, blockages, frozen macros). Deposit their area, then
   **clamp the fixed baseline** (§3 — this is the crux of the whole report).
2. **Pass 2 — movable cells and fillers**, deposited *on top* of the clamped fixed baseline.

The ordering is what makes overflow mean the right thing: a bin overflows only when **movable**
density is stacked above what the fixed macro already occupies — not merely because a macro sits
there. This encodes the physical intuition *"a bare macro is not overflow; cells piled onto it
are."*

---

## 3. Building ρ, part 2 — the fixed-density cap (`min(ρ, td)`), and why it diverges from XPlace

This is the part worth preserving carefully, because it is subtle and it is where AIEplace now
**deliberately departs from XPlace**.

### 3.1 The problem it solves

Designs have a **target density** `td ∈ (0, 1]` — the fraction of each bin cells are allowed to
fill (whitespace is left for routing). Overflow is measured against `td`: a bin contributes
overflow when its density exceeds `td`.

Now consider a bin **fully covered by a fixed macro**, on a design with `td = 0.65`. Its raw
`ρ = 1`. Measured naively against `td`, it looks `1 − 0.65 = 0.35` *over budget* — permanently, on
every iteration, even though nothing is wrong: a macro is simply there, and no legalizer can
"spread" a fixed macro. Global placement would chase an overflow it can never remove. So the fixed
contribution must be **clamped** before movable cells are added. The question is *how*.

### 3.2 Two clampings that agree at the ends and differ in the middle

Let `ρ` be a bin's fixed-coverage fraction (0 = empty, 1 = fully covered by macro) 

| | formula | name |
|---|---|---|
| **AIEplace (now)** | `min(ρ, td)` | the **cap** |
| **XPlace** | `min(ρ, 1) · td` | the **scale** |

Both agree at the extremes:

- **Empty bin** (`ρ=0`): both give 0.
- **Fully covered** (`ρ=1`): cap `= min(1,td) = td`; scale `= 1·td = td`. **Equal** — a full macro
  bin lands exactly at the target and contributes zero overflow. *Both formulas solve §3.1.*

They differ only for a **partially** covered bin — a macro **perimeter**. There,

```
gap  =  cap − scale  =  ρ · (1 − td)      (for ρ ≤ td)
```

The `(1 − td)` factor is the whole story: the gap is **zero at td = 1** and grows as td falls. It
is also multiplied by ρ, so it is largest at *partial* coverage. Concretely, a bin **half-covered**
by a macro (`ρ = 0.5`):

| td | scale reports | cap reports | gap |
|---|---|---|---|
| 1.0 | 0.50 | 0.50 | **0** |
| 0.8 | 0.40 | 0.50 | 0.10 |
| 0.5 | 0.25 | 0.50 | **0.25** |

### 3.3 What the gap *means*: phantom headroom

Overflow triggers once a bin's density exceeds `td`, so the fixed baseline decides **how much
movable density a bin still invites**. Half-covered, `td = 0.5`:

- The **cap** reports 0.5 = td → *"this bin is at capacity, no room for cells."*
- The **scale** reports 0.25 → *"there is 0.25 of headroom before this bin overflows."*

That 0.25 is **phantom** — the macro physically fills that half of the bin. Under the scale the
optimizer is told there is room where a hard blockage sits, so cells crowd the macro's perimeter,
and the legalizer later pays to move them out. Under the cap, cells are pushed away from the macro
from the start. **Lower td ⇒ larger phantom headroom ⇒ the cap helps more.** Designs that are both
**low-td and macro-heavy** (perimeters everywhere) feel it most.

### 3.4 Why XPlace uses the scale

There is **no comment in XPlace justifying the choice** — the line is bare
(`init_density_map.clamp_(0,1).mul_(target_density)`). The reason is implicit in the *model*:
XPlace runs a **single scalar charge field with one uniform target — density `td` everywhere**.
Fixed cells are not special; they are just charge added to the same field. Multiplying the
normalized fixed map by `td` is the **linear, uniform** transform that (a) makes a fully-blocked
bin land exactly on that uniform target (zero net force) and (b) keeps the whole field on the same
`[0, td]` scale as everything else — which is what the FFT/Poisson solver wants. A per-bin `min`
against `td` is a **non-linear** clamp that treats fixed cells as a *hard blockage*, a special case
outside "everything is charge pushed to a uniform td." So the scale is the choice *consistent with
the electrostatic formulation*; the cap is the more *physically literal* reading of a macro.

### 3.5 AIEplace's decision (measured)

AIEplace shipped the faithful **scale** for eight days, then reverted to the **cap** — a
deliberate, documented divergence — because on a 16-design macro-heavy suite the cap is worth
**−2.38 pp of mean post-legalization wirelength ratio**, entirely on the td<1 designs (the td=1
half is bit-for-bit identical, since the two formulas coincide at td=1). The trade is that the
*scale* was observed to *help* low-td **standard-cell** designs (few macros ⇒ few perimeter bins ⇒
the smoother, tilted-down field spreads cells better) — so the choice is a genuine
macro-heavy-vs-standard-cell trade, not a strict improvement. That trade is design-class dependent
and is the kind of decision that must be made with numbers, not first principles.

---

## 4. From ρ to the force — why a 2-D DCT "outputs the field gradient"

We have a smooth charge density ρ on the bin grid. We need the **electric field E = (E_x, E_y)**
at every bin — that is the density gradient the optimizer adds to its step. E comes from solving
**Poisson's equation** and taking a gradient:

```
∇²ψ = −ρ        (potential ψ from charge ρ)
E   = −∇ψ       (field is minus the gradient of the potential)
```

Solving a Poisson PDE on the whole grid every iteration sounds expensive. The spectral (DCT)
method makes it nearly free, and the reason is the heart of this section.

### 4.1 Why cosines (DCT), not Fourier

The die is an **insulating box**: charge does not flow out through the boundary, so the field's
component *normal to the die edge is zero* (a Neumann / zero-flux boundary condition). The basis
functions that natively satisfy "zero normal derivative at the boundary" are **cosines**. So ρ is
expanded in a **cosine series** — a Discrete Cosine Transform — not a full Fourier series. (This
is also why the inverse step mixes cosine and sine transforms; see §4.4.)

### 4.2 Forward: ρ → spectral coefficients `a_uv`

A 2-D DCT of ρ yields coefficients `a_uv`, one per spatial frequency `(u, v)`:

```
a_uv = (1 / M²) · Σ_x Σ_y  ρ[x][y] · cos(w_u·x) · cos(w_v·y),   w_u = 2π·u / M
```

(DREAMPlace Eq 3a.) It is computed **separably** — a 1-D DCT along every row, a transpose, a 1-D
DCT along every row again — so the 2-D transform costs `O(N log N)` via FFT rather than `O(N²)`.

### 4.3 The trick: differentiation becomes multiplication

In the cosine basis the Laplacian is **diagonal**. Each mode `cos(w_u x)cos(w_v y)` is an
eigenfunction of `∇²` with eigenvalue `−(w_u² + w_v²)`. So Poisson's equation, a coupled PDE in
real space, becomes a **per-mode division** in the spectral domain:

```
ψ_uv = a_uv / (w_u² + w_v²)
```

And the gradient is likewise diagonal — differentiating `cos(w_u x)` gives `−w_u·sin(w_u x)`, i.e.
**multiplication by the frequency `w_u`**. So the field's spectral coefficients are just:

```
Ex_uv = a_uv · w_u / (w_u² + w_v²)
Ey_uv = a_uv · w_v / (w_u² + w_v²)
```

**This is the answer to "why does the DCT output the field gradient."** Once ρ is in the spectral
domain, *both* operations the field needs — the Poisson solve (divide by the eigenvalue) and the
gradient (multiply by the frequency) — are **elementwise scalings per frequency**. The transform
diagonalizes the whole problem. A global PDE-plus-gradient collapses to: transform in, scale each
coefficient, transform back. (The `(0,0)` mode is skipped — its eigenvalue is 0, and it is the
constant offset, which carries no force.)

### 4.4 Inverse: spectral field → per-bin `E_x`, `E_y`

Bringing the field back to real bins uses **inverse transforms** — but with a cosine/sine twist.
Because `E_x` is the *x-derivative* of a cosine expansion, its x-dependence is now a **sine**
series while its y-dependence stays cosine:

```
E_x[x][y] = Σ_uv Ex_uv · sin(w_u·x) · cos(w_v·y)     → inverse SINE transform in x, cosine in y
E_y[x][y] = Σ_uv Ey_uv · cos(w_u·x) · sin(w_v·y)     → cosine in x, inverse SINE transform in y
```

That sin/cos asymmetry is exactly the `IDCT` / `IDXST` pairing in the code (an inverse DCT along
one axis, an inverse "DXST" sine transform along the other). Each is again `O(N log N)`.

> **One numerical footnote that matters:** the forward transform already carries the `1/M²`
> normalization, so the inverse transforms are left **unnormalized** on purpose. Re-normalizing
> them would apply `1/N` twice, inflating the field by ~`N²` and corrupting the optimizer's
> preconditioner (the distortion grows with grid size). "Unnormalized inverse" is not a bug; it is
> what keeps the field faithful to the reference.

### 4.5 The field becomes the step

The per-bin `(E_x, E_y)` is the density gradient. Each cell **gathers** the field at its location,
that becomes its density force, and the force is added to the wirelength gradient (weighted by the
density weight `λ`) to form the iteration's step. Then cells move, ρ is rebuilt, and the loop
repeats.

---

## 5. Where this lives in the code (two ρ builds, one formula)

AIEplace builds ρ in **two** places, for two purposes, and they **must apply the identical
fixed-density formula**:

1. **`computeOverlaps` → `Grid::clampFixedDensity`** — builds the persistent ρ in the grid that
   the DCT solve (§4) consumes. This is the field the optimizer minimizes.
2. **`computeOverflow`** — builds an *independent* ρ for the **metric / convergence signal**: it is
   the smoothed overflow that tells global placement when it has converged and drives the schedule.
   It re-implements the deposit inline because it also needs on-demand variants (smoothed vs exact
   footprints; fillers included/excluded; movable macros excluded) that must not disturb the
   solver's grid state.

If these two disagree, the placer **optimizes one density map and decides when to stop from a
different one** — which is exactly the bug that hid here for four days when only one site was
updated. Two further copies live in the hardware path (the synthesizable HLS module and its
reference model), so the same formula exists in **four** places total and every one must move
together. The canonical definition and this warning live at `Grid::clampFixedDensity`.

---

## 6. Summary slide

- **ρ** is a smoothed cell-area density on an `M×N` bin grid; cells are inflated to ≥√2 bins
  (area-conserving) so the field is smooth.
- Fixed macros are deposited first and **clamped** so a bare macro is not counted as overflow; only
  movable cells stacked above it are.
- **The clamp is the divergence.** AIEplace caps at `min(ρ, td)` (macro = hard blockage); XPlace
  scales by `min(ρ,1)·td` (macro = uniform charge). They agree at full coverage and at td=1, and
  differ at macro perimeters by `ρ(1−td)`. The cap removes "phantom headroom" at macro perimeters,
  worth **−2.38 pp** on a macro-heavy low-td suite; the scale helps low-td standard-cell designs.
  Deliberate, measured, documented divergence.
- The **field** E is obtained from ρ by a 2-D **DCT**: in the cosine spectral domain the Poisson
  solve is a divide-by-eigenvalue and the gradient is a multiply-by-frequency — both elementwise —
  so `DCT → scale each mode → inverse (I)DCT/(I)DXST` yields the per-bin force in `O(N log N)`.

---

## Appendix — glossary

| symbol / term | meaning |
|---|---|
| **ρ** | bin density = deposited cell area / bin area; `ρ=1` is a full bin |
| **td** | target density ∈ (0,1]; the per-bin fill budget, whitespace left for routing |
| **the cap** | `min(ρ, td)` — AIEplace's fixed-macro clamp (macro as blockage) |
| **the scale** | `min(ρ,1)·td` — XPlace's fixed-macro clamp (macro as uniform charge) |
| **√2 inflation** | expanding a footprint to ≥√2 bins/dim with area-conserving weight, to smooth ρ (XPlace `expand_ratio`) |
| **overflow** | Σ over bins of `max(0, ρ_bin − td)`, normalized by movable area; the convergence signal |
| **a_uv** | spectral (DCT) coefficient of ρ at frequency (u,v) |
| **w_u** | angular frequency `2π·u/M` of mode u |
| **E = (E_x,E_y)** | electric field = density gradient = the force the optimizer adds each step |
| **λ** | density weight; scales the density force against the wirelength gradient |
