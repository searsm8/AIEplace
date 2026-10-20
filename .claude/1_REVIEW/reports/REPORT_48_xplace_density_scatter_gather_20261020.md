# REPORT #48: how XPlace scatters the bin density and gathers the field

*2026-10-20. Read-only analysis of XPlace @ `~/phd/Xplace` (no code changed here or there). Feeds the
`bin_density_CU` structure decision in [[PLAN_48_density_onchip_20261014.md]] and
[[REPORT_48_bin_density_fixed_point_20261019.md]]; cost numbers come from
[[_NEW_REPORT_xplace_gpu_iteration_breakdown_20261013.md]] (RTX 3080, bigblue4, `--deterministic True`).*

Paths below: `cu/` = `~/phd/Xplace/cpp_to_py/density_map_cuda/`, `src/` = `~/phd/Xplace/src/`.

## Bottom line

1. **XPlace's "deterministic" scatter is still global atomics: integer ones.** One 2×2 group of threads
   per node, each `atomicAdd`-ing an `unsigned long long` fixed-point value straight into a global
   per-bin array (`cu/density_map_cuda_kernel.cu:79-118`). No sorting by bin, no per-bin ownership,
   no two-pass scheme, no privatized copies. Determinism comes only from integer addition being
   associative, which is exactly the argument behind our 18.18 accumulator.
2. **The format is 32.32 unsigned in a uint64**, and in practice it is 32.32 for every grid up to 2³¹ bins
   (`cu/density_map_cuda_kernel.cu:291-293`). Each deposit is computed in float and then **truncated**
   (`:112-113`). The map is converted float→uint64 before the scatter and uint64→float after it, in two
   extra kernels (`:229-243`, `:311-322`).
3. **The non-deterministic path is the same kernel with `float atomicAdd`** (`:40-77`). The deterministic
   switch adds two copy kernels and doubles the atomic width; it does **not** change the parallel structure
   of the scatter.
4. **The gather is where determinism costs parallelism.** The deterministic backward kernel uses **one
   thread per node** that loops serially over the node's footprint (`:183-227`). The non-deterministic one
   uses the same 2×2 threads per node as the scatter and merges their 4 partial sums with float
   shared-memory atomics (`:120-181`), which is order-dependent.
5. **The sort XPlace does (`sorted_node_map`) is by node AREA, not by bin**, descending
   (`src/database.py:773-788`). It groups similar footprint sizes into the same warp (fewer divergent
   loop trip counts). It has nothing to do with determinism.
6. **Three maps, one kernel.** Fixed nodes go into `init_density_map` once (`src/initializer.py:7-102`);
   each iteration scatters movable cells on top of a clone of it, scatters fillers into a separate zeroed
   map, and adds the two in float (`src/core/electronic_density_layer.py:120-134`). The overflow is
   measured on the movable+fixed map without the fillers.
7. **On bigblue4 the cost is set by memory locality, not arithmetic.** The filler scatter launch takes
   **3.14 ms** for 2.89 M fillers, but the movable scatter takes **0.71 ms** for 2.17 M cells. Same kernel,
   same footprint size; the fillers start uniformly random over the die and stay uncorrelated with their
   index, so their atomics miss L2. The gather (5.29 ms, all 5.05 M nodes in one launch) is latency-bound:
   one thread per node, serial dependent-address loads, and it reads two planes.
8. **For `bin_density_CU`:** XPlace confirms the arithmetic (integer accumulation, footprint stretched to
   √2 bins, mass preserved by `expand_ratio`, fixed map built once). It gives no structural precedent for
   the banked grid. The GPU's cost driver (cache misses on a randomly ordered stream) does not exist in an
   on-chip URAM grid, so XPlace's ms numbers say nothing about how fast our structure needs to be beyond
   the per-node targets in §6.

## 0. Call path per iteration

`calculator.py:calc_obj_and_grad` asserts the merged path (`src/calculator.py:31-32`), then
calls `density_map_layer.merged_density_loss_grad(...)` (`src/calculator.py:81-84`). That runs
`merged_density_loss_grad_main` (`src/core/electronic_density_layer.py:93-159`):

```python
# src/core/electronic_density_layer.py:115-134
normalize_node_info = density_map_cuda.pre_normalize(node_pos, node_size, node_weight, expand_ratio, unit_len, ...)
mov_density_map = init_density_map.clone()
mov_density_map = density_map_cuda.forward(normalize_node_info[mov_lhs:mov_rhs], mov_conn_sorted_map, mov_density_map, ...)
if filler_sorted_map is not None:
    filler_density_map = torch.zeros_like(init_density_map)
    filler_density_map = density_map_cuda.forward(normalize_node_info[mov_rhs:], filler_sorted_map, filler_density_map, ...)
    density_map = filler_density_map
    density_map.add_(mov_density_map)
```

then the four spectral transforms (`:140-147`), the energy (`:149`), and one gather over **all** nodes
(movable cells and fillers together) with `grad_weight = -1` (`:151-156`). `mov_density_map` is cached
(`:157-158`, `:333-334`) so the evaluator's overflow (`direct_calc_overflow`, `:265-293`) does not
re-scatter. The older autograd path (`ElectronicDensityFunction`, `:9-91`) does the same work split over
forward/backward but is not what the GP loop calls.

Per iteration on bigblue4 that is 1 pre-normalize + 2 scatters (each wrapped by 2 copy kernels) + 1
gather, matching the profiler's launch counts (100 iters: normalize 100, copyFrom 200, forward 200,
copyTo 200, backward 100; from `.claude/2_ARTIFACTS/xplace_profile/bigblue4/trace.json`).

## 1. The scatter algorithm

### 1a. Node → bin-unit rectangle (pre-normalize, one thread per node)

```cpp
// cu/density_map_cuda_kernel.cu:27-36
normalize_node_info[i][0] = (node_pos[i][0] - node_size[i][0] / 2) / unit_len[0];  // x_l
normalize_node_info[i][1] = (node_pos[i][0] + node_size[i][0] / 2) / unit_len[0];  // x_h
normalize_node_info[i][2] = (node_pos[i][1] - node_size[i][1] / 2) / unit_len[1];  // y_l
normalize_node_info[i][3] = (node_pos[i][1] + node_size[i][1] / 2) / unit_len[1];  // y_h
normalize_node_info[i][4] = node_weight[i] * expand_ratio[i];                      // weight
if (... width < 0 || height < 0 || (w < 1e-6 && h < 1e-6)) normalize_node_info[i][4] = -normalize_node_info[i][4];
```

Positions are centres; dividing by `unit_len` (the bin size) puts every bin at 1×1, so bin `j` spans
`[j, j+1)`. A non-positive weight flags "skip this node" (`:35`); later kernels test `weight > 0`. The
5-float record (20 B/node) is written once and read by both scatter and gather. `node_size` here is
already the **clamped** size (§3), so the stretch happens before this kernel.

### 1b. Node → bins (scatter kernel)

```cpp
// cu/density_map_cuda_kernel.cu:87-117 (deterministic; the float one at :47-76 is identical but for the add)
const int index = blockIdx.x * blockDim.z + threadIdx.z;            // one node per threadIdx.z
const int i = (sorted_node_map[index] >= 0) ? sorted_node_map[index] : index;
...
int x_lf = lround(floor(x_l)); ... x_lf = max(x_lf, 0); x_hf = min(x_hf, num_bin_x - 1); ...
for (int j = x_lf + threadIdx.y; j < x_hf + 1; j += blockDim.y) {   // 2 threads stride the x bins
    float overlap_x = overlap(x_l, x_h, bin_x_l);
    for (int k = y_lf + threadIdx.x; k < y_hf + 1; k += blockDim.x) { // 2 threads stride the y bins
        float overlap_area = overlap_x * overlap(y_l, y_h, bin_y_l);
        atomicAdd(&aux_mat[j * num_bin_y + k],
                  static_cast<unsigned long long>(weight * overlap_area * scalar));
    }
}
```

Launch: `blockSize(2, 2, 64)`, `block_count = ceil(num_nodes / 64)` (`:284-286`), and
`__launch_bounds__(256, 4)` (`:79`). So **each node gets a 2×2 group of threads**; for a footprint of
≤ 2×2 bins each thread does one deposit, for a 3×3 footprint the threads stride (one does 4, two do 2, one
does 1). Large nodes (macros) loop over their whole footprint with the same 4 threads.

- `overlap(x_l, x_h, j) = min(x_h, j+1) − max(x_l, j)` (`:9-12`), in float, in bin units.
- The bin range is clamped to the grid but the overlap uses the unclamped edges, so the part of a node
  outside the die is simply not deposited.
- Map layout is **x-major**: `aux_mat[j * num_bin_y + k]`, y contiguous (`:112`). The two `threadIdx.x`
  threads hit adjacent y bins, i.e. adjacent 8-byte words.
- The per-bin sum is a **global-memory atomic**, nothing else: no shared-memory tile, no per-block
  privatized grid, no reduction pass.

### 1c. How the deterministic kernel avoids order dependence

It does not avoid atomics; it makes them commutative.

```cpp
// cu/density_map_cuda_kernel.cu:288-322
int max_value_bits = max(static_cast<int>(ceil(log2((num_bin_x + 0.1) * (num_bin_y + 0.1)))) + 1, 32);
int scalar_bits = max(64 - max_value_bits, 0);
unsigned long long scalar = (1UL << scalar_bits);
float inv_scalar = 1.0 / static_cast<float>(scalar);
...
static unsigned long long *aux_mat_uint64_ptr = nullptr;   // cached scratch, realloc only on grid change
copyFromFloatAuxMat<<<...>>>(aux_mat_uint64_ptr, aux_mat.data_ptr<float>(), scalar, inv_scalar, num_bin);
density_map_cuda_deterministic_forward_kernel<<<block_count, blockSize, 0, stream>>>(..., aux_mat_uint64_ptr, ..., scalar);
copyToFloatAuxMat<<<...>>>(aux_mat_uint64_ptr, aux_mat.data_ptr<float>(), scalar, inv_scalar, num_bin);
```

- **Integer bits.** The comment (`:290`) bounds the largest possible bin value by the total number of bins
  (every node's mass in one bin, ≤ die area in bin units). That gives `ceil(log2(N_bins)) + 1` = 21 bits at
  1024², 23 at 2048², but it is then **floored at 32**. So `scalar = 2³²` and the format is **32.32** for
  any grid below 2³¹ bins. Unsigned: valid because every accumulated weight is positive (`:90-91`) and the
  fixed base map is clamped to `[0, 1]·td` (`src/initializer.py:82`).
- **Per-deposit conversion.** `weight * overlap_area * scalar` is evaluated in **float** (24-bit mantissa)
  and then truncated toward zero (`:113`). For a deposit near 1 bin the float value is ~4·10⁹, whose ulp is
  256 lsb, so the 32 fraction bits carry **no more per-deposit precision than float** (relative ~6·10⁻⁸).
  The integer format buys order independence, not precision.
- **Base map in.** `copyFromFloatAuxMat` (`:229-235`) truncates the float map (for the movable map that is
  the clone of `init_density_map`) to 32.32. This happens every iteration, on the same float values, so it is
  deterministic but repeated work.
- **Result out.** `copyToFloatAuxMat` (`:237-243`): `inv_scalar * aux_mat_uint64[i]`, i.e. the uint64
  converted to float (round to nearest) times an exact power of two. Downstream everything is float32.
- The cached `static` scratch buffer is shared by the movable and filler calls; that is safe only because
  both run on the one stream.

## 2. The non-deterministic path, and what changes

`density_map_cuda_forward_kernel` (`cu/density_map_cuda_kernel.cu:40-77`) has the same launch, footprint
and loop; only the add differs:

```cpp
atomicAdd(&aux_mat[j * num_bin_y + k], weight * overlap_area);   // :72, float32 atomic into the float map
```

| | non-deterministic | deterministic (XPlace default, `main.py:18`) |
|---|---|---|
| scatter threads/node | 2×2 | 2×2 (same) |
| scatter atomic | float32 `atomicAdd` | uint64 `atomicAdd` |
| scatter extra kernels | none | `copyFromFloatAuxMat` + `copyToFloatAuxMat` per call |
| scatter memory | float map (4 B/bin) | float map + uint64 scratch (12 B/bin) |
| gather threads/node | 2×2, partials merged by float shared atomics (`:147-178`) | **1**, serial loop (`:192-224`) |
| gather block | `(2,2,64)` + 512 B shared (`:380-384`) | 64 threads (`:368-370`) |

On bigblue4 the two copy kernels cost 0.150 + 0.157 = **0.31 ms/iter** (4 × 33.5 MB passes over the
2048² map), 7% of the 4.16 ms scatter total. The non-deterministic mode was not measured.

`forward_naive` (`cu/density_map_cuda_naive_kernel.cu:8-130`) is a third variant, **one thread per node**
with no area sort and an optional in-kernel clamp. GP does not use it per iteration; it builds the fixed
map once (§3c).

## 3. Movable cells, fillers, fixed nodes, and the √2 stretch

### 3a. The footprint stretch (ePlace's "local smoothing")

It is done in Python, once, not in the kernel:

```python
# src/database.py:911-923
expand_ratio = mov_node_pos.new_ones((mov_node_pos.shape[0]))
if self.clamp_node:                                       # --clamp_node default True (main.py:34)
    self.mov_node_size_real = mov_node_size.clone()
    mov_node_area = torch.prod(mov_node_size, 1)
    clamp_mov_node_size = mov_node_size.clamp(min=self.unit_len * math.sqrt(2))
    clamp_mov_node_area = torch.prod(clamp_mov_node_size, 1)
    expand_ratio = mov_node_area / clamp_mov_node_area
    mov_node_size = clamp_mov_node_size
if args.target_density < 1.0:
    expand_ratio[mov_lhs:mov_rhs].masked_fill_(self.is_mov_macro[mov_lhs:mov_rhs], args.target_density)
```

- Each side is widened **independently** to at least √2 bins; the density is then scaled by
  `expand_ratio = real area / stretched area` so the deposited mass equals the real area. The kernel sees
  only `weight = node_weight · expand_ratio` (`cu/…_kernel.cu:31`) and the stretched rectangle.
- Movable macros under `td < 1` get `expand_ratio = td` instead (they are never stretched; this scales their
  mass to td).
- Consequence for footprint size: a side of length ≤ √2 bins touches 2 bins with probability 2−√2 ≈ 0.59
  and 3 with probability √2−1 ≈ 0.41 (uniform offset), so **≤ 3×3 bins, mean 2.414² ≈ 5.83 bins** for every
  std cell and filler smaller than √2 bins on both sides. That holds at any grid as long as the cell is
  sub-√2-bin, which is why the per-node work does **not** grow as the grid gets finer.
- `expand_ratio` is recomputed when routability inflation resizes cells (`src/core/route_force.py:693`);
  not in the default flow.

### 3b. Movable vs filler: two maps, one kernel

`compute_sorted_node_map` (`src/database.py:773-788`) makes three index arrays: all movable+filler nodes by
area descending (`mov_sorted_map`), movable cells only (`mov_conn_sorted_map`), fillers only
(`filler_sorted_map`). Per iteration:

- movable map = `init_density_map.clone()` + scatter(movable cells) (`electronic_density_layer.py:120-124`)
- filler map = zeros + scatter(fillers) (`:126-130`)
- `density_map = filler + movable` in float (`:131-132`) → the field solve
- overflow uses the **movable map alone** (which includes the fixed base):
  `((mov_density_map − td) · bin_area).clamp(min=0).sum() / total_mov_area_without_filler`
  (`src/run_placement_nesterov.py:41-43`).

Each map is quantized to float separately before the float add, so the sum is deterministic but not equal to
a single integer accumulation of everything.

### 3c. Fixed nodes: `init_density_map`, built once

```python
# src/initializer.py:29-33, 82
init_density_map = density_map_cuda.forward_naive(
    node_pos, node_size, node_weight, data.unit_len, zeros_density_map,
    data.num_bin_x, data.num_bin_y, node_pos.shape[0], -1.0, -1.0, 1e-4, False,   # clamp_node=False
    args.deterministic)
...
init_density_map.clamp_(min=0.0, max=1.0).mul_(args.target_density)
```

Fixed nodes are **not stretched** (`clamp_node=False`), weight 1, margin 1e-4 bin; then each bin is clamped
to `[0, 1]` and **scaled** by td. That scale is the one AIEplace deliberately replaced with a cap
`min(ρ, td)` (CLAUDE.md, *Deliberate divergences*, TODO #35); compare against XPlace with that in mind.
Under routability/inflation options the map also gets snet blockages and inflated IO pins (`:39-100`), not
in the default flow. Movable macros are folded in only after `zero_macro_grad` (`:21-25`, mixed-size).

## 4. The gather (electric force)

Input: `grad_mat`, a `2 × M × N` float tensor = `[Ex; Ey]` stacked contiguously
(`electronic_density_layer.py:142-147`). One launch over **all** nodes in `mov_sorted_map` order:

```cpp
// cu/density_map_cuda_kernel.cu:192-224 (deterministic: 1 thread per node, 64 threads/block)
const int index = blockIdx.x * blockDim.x + threadIdx.x;
const int i = (sorted_node_map[index] >= 0) ? sorted_node_map[index] : index;
...                                                         // same floor/clamp footprint as the scatter
float gradX = 0, gradY = 0;
for (int j = x_lf; j < x_hf + 1; j++) {
    float overlap_x = overlap(x_l, x_h, bin_x_l);
    for (int k = y_lf; k < y_hf + 1; k++) {
        float overlap_area = overlap_x * overlap(y_l, y_h, bin_y_l);
        gradX += grad_mat[0 * num_bin_x * num_bin_y + j * num_bin_y + k] * overlap_area;
        gradY += grad_mat[1 * num_bin_x * num_bin_y + j * num_bin_y + k] * overlap_area;
    }
}
node_grad[i][0] = grad_weight * weight * gradX;
node_grad[i][1] = grad_weight * weight * gradY;
```

- **Same footprint** as the scatter: it reads the same `normalize_node_info` record, so the stretched
  rectangle and `weight = node_weight · expand_ratio` apply. The force on a stretched cell is
  `−expand_ratio · Σ overlap · E`, i.e. the real area times the field averaged over the stretched footprint.
- **Deterministic without integers**: no atomics, each thread sums its own bins in a fixed loop order. This
  is the natural form for a gather.
- The non-deterministic kernel (`:120-181`) splits a node's footprint over 2×2 threads like the scatter and
  merges the 4 partials with `atomicAdd` on shared-memory floats (`:171-172`). Float-add order varies
  between runs; that is the only reason it is non-deterministic.
- `grad_weight = −1` (`electronic_density_layer.py:151`); `density_weight` (λ) is applied outside
  (`src/calculator.py:84`).

## 5. Memory traffic and parallel structure: why 0.71 + 3.14 ms and 5.29 ms

bigblue4 basis (from `.claude/2_ARTIFACTS/xplace_profile/bigblue4.prof.log` and `trace.json`): 2048² bins,
`unit_len` 15.7 × 15.8, **2 169 183 movable cells + 2 885 578 fillers = 5.05 M nodes**, 8 170 fixed
macros (all in the base map). Filler 10.4 × 12.0 = 0.66 × 0.76 bins; std cells are one row (0.76 bin) tall.
Both are stretched to √2 × √2 bins, so I take **~5.8 bins per node ≈ 29 M deposits per iteration**
(estimate; ignores the few cells wider than √2 bins).

Per-launch kernel times (100 iterations, my split of the profiler trace by grid size):

| kernel | grid × block | nodes | ms/iter | ns/node | ns/bin (5.8 bins) |
|---|---|---|---|---|---|
| pre-normalize | 39 491 × 128 | 5.05 M | 0.376 | 0.07 | |
| scatter, movable | 33 894 × (2,2,64) | 2.17 M | **0.710** | 0.33 | 0.056 |
| scatter, fillers | 45 088 × (2,2,64) | 2.89 M | **3.143** | 1.09 | 0.19 |
| copyFrom + copyTo (×2 calls) | 8 192 × 512 | 4.2 M bins | 0.308 | | |
| gather | 78 981 × 64 | 5.05 M | **5.288** | 1.05 | 0.18 |

Map sizes: the uint64 scratch is 2048² × 8 B = **33.5 MB**, the float `[Ex;Ey]` is also **33.5 MB**; the
RTX 3080's L2 is 5 MB. Neither fits, so every access that misses L2 pulls at least one 32-byte DRAM sector.

**Scatter: locality decides it.** The movable and filler launches run identical code on identical
footprints, yet a filler costs 3.3× a movable cell. What differs is where consecutive nodes land:
- Fillers start **uniformly random over the whole die** (`src/database.py:873-880`) and filler ids are not
  re-ordered spatially (`filler_sorted_map` is an area sort over identical areas). Consecutive threads hit
  unrelated parts of a 33.5 MB array: nearly every atomic is an L2 miss, i.e. a DRAM read-modify-write.
- Movable cells start in a Gaussian of σ = 0.001 × die at the centre (`src/database.py:889-892`). In the
  measured window (iterations 200-300, overflow 0.81) they still occupy a fraction of the die, so their
  working set is much closer to L2-resident.
- Rough floor for the fillers: ~2.4 columns × ~1.2 sectors × 64 B (RMW) ≈ 185 B per filler → 0.53 GB/iter →
  0.7 ms at 760 GB/s. Measured 3.1 ms is ~4× that, consistent with L2 atomic-unit / partition throughput on
  random 64-bit atomics rather than raw DRAM bandwidth.

*Explained by inference, not measured: `nsys`/`ncu` are not installed, so hit rates were not read. A
direct test would be the late-window trace split the same way (cells more spread → movable launch should
slow down), or an `ncu` run of the two launches.*

**Gather: latency, and a quarter of the threads.** Per node it reads the 20 B record (through the
`sorted_node_map` indirection), then ~2.4 columns × 2 planes, each ~1-2 sectors, ≈ 180-250 B, and writes
8 B to a scattered `node_grad[i]`. That is ~1.1 GB/iter, ~1.5 ms at peak bandwidth; measured 5.3 ms.
Two things make it slower than its byte count and slower than the scatter:
- **No fire-and-forget.** A scatter `atomicAdd` whose return is unused compiles to a reduction (`RED`) the
  thread does not wait for; the gather's loads must return before the sum continues. Each thread issues a
  variable-trip loop of ~6 iterations × 2 loads with the address computed in the loop, so it spends most of
  its time waiting on DRAM latency.
- **One thread per node** (the deterministic choice, `cu/…_kernel.cu:367-370`) versus 4 for the scatter: the
  same node population gives a quarter of the threads, hence a quarter of the loads in flight to hide that
  latency. 64-thread blocks also cap residency (16 blocks/SM on Ampere → 1024 threads/SM).
- The node order mixes fillers and cells (one global area sort), so most warps contain random-die fillers.

In short: **the scatter is bounded by the random-access throughput of global atomics and the gather by
load latency with too few threads.** Neither is bounded by arithmetic, and neither depends on grid size
except through cache misses.

## 6. What maps onto `bin_density_CU`, and what argues against it

Our design for reference (`pl/src/modules/bin_density_CU.hpp`, [[REPORT_48_bin_density_fixed_point_20261019.md]]):
one 36-bit URAM word per bin, 18.18 unsigned, `q(o) = (uint64)(o + 0.5f)`, fixed base quantized once, a
single accumulator for cells then fillers with the overflow excess read before the fillers, one long
slot-ordered stream; next, a 4×4-interleaved banked grid with forwarding, then multiple lanes.

**Maps onto it (XPlace does the same thing):**
- **Integer accumulation for order independence.** XPlace's deterministic mode rests on precisely this; it
  adds no sorting or ownership on top. Our "any number of lanes, any order, still bit-exact" argument is the
  one XPlace relies on.
- **Per-deposit float, then convert.** XPlace forms `weight · overlap_x · overlap_y · 2³²` in float and
  converts per deposit; so do we (with `2^F / bin_area` folded into the footprint weight). XPlace's 32 fraction
  bits are mostly float rounding noise, which supports F = 18 not being a loss of meaningful precision
  relative to XPlace for large deposits.
- **The stretch is a fixed property of the node, applied once.** XPlace clamps sizes once in Python and
  passes the scale as `weight`; our per-slot footprint table is the same idea, precomputed.
- **Footprint ≤ 3×3 for every sub-√2-bin node.** That is the case the 4×4 bank interleave serves with room
  to spare (9 of 16 banks per node). XPlace's 2×2 threads per node are its version of "spread one node's
  bins across parallel units".
- **Fixed map built once, cells on top, fillers separate for the overflow.** Same split as ours. Ours reads
  the excess before the fillers on one accumulator; XPlace scatters into a second map. Same quantity.
- **Gather = same footprint, read-only.** XPlace's deterministic gather is a plain per-node read-and-sum with
  no hazards. On the FPGA the same banked layout serves it at one node per cycle with **no forwarding**
  (nothing is written), and Ex and Ey can be two banked arrays read in the same cycle.

**Argues against copying XPlace (or limits what its numbers mean):**
- **XPlace has no structural answer to bank conflicts or read-modify-write hazards**; the L2 atomic units
  absorb them. The banking + forwarding design is ours alone; XPlace neither supports nor contradicts it.
- **Its cost driver does not exist on chip.** bigblue4's scatter time is mostly fillers missing L2; a URAM
  grid has uniform access cost, so spatial order of the stream does not matter to us (it would only
  matter for forwarding distance; see below). Don't read the GPU's 3.85 ms as a property of the scatter.
- **Area sort is for warp divergence, not for us.** The FPGA pipeline has no warps; the analog is
  separating the rare large footprints (wider than 4 bins after the stretch, i.e. movable macros) into a
  slow path so they don't stall the one-node-per-cycle pipe. XPlace pays for them by looping inside one
  2×2 thread group.
- **Truncation vs our round-to-nearest, and 32.32 vs 18.18.** Both deterministic, so neither is a bug,
  but XPlace's map is ≤ 1 lsb (2⁻³²) per deposit below exact and ours is unbiased at 2⁻¹⁸. Against XPlace this
  is below every tolerance we use; just don't expect bit equality with XPlace's map.
- **The per-iteration float↔uint64 round-trip** is a GPU artefact (the map must be float for cuFFT and the
  next clone). Our base map is quantized once and the accumulator stays integer until ρ is read out.

**Throughput targets implied by XPlace (bigblue4, ~5.8 bins/node, same per node at 1024² because of the
√2 stretch):** GPU scatter 3.85 ms for 5.05 M nodes = **0.76 ns/node (1.3 G nodes/s)**; gather 5.29 ms =
**1.05 ns/node**. A one-node-per-cycle banked CU at 300 MHz does 0.3 G nodes/s → **~17 ms** per scatter for
bigblue4's node count, ~4.4× the GPU. Matching the GPU's scatter needs **~4-5 lanes at 300 MHz**; the gather,
read-only, needs ~3-4 read lanes. The GPU is not the limit we must hit (the VCK budget is the
max(WL, density) critical path), but it sizes "multiple lanes" as single digits, not tens.

**What lanes would cost, given XPlace's lesson.** XPlace gets parallel scatter by letting hardware
atomics resolve collisions in a shared array. The FPGA options that keep bit-exactness are (a) one banked
grid with N lanes arbitrated across 16 banks (conflicts when two lanes' 3×3 windows overlap mod 4, which a
random-order stream makes frequent), (b) N privatized grids merged at the end (exact because integer;
128 URAMs per 1024² copy, so ~3 copies is the URAM ceiling), or (c) partition the die among lanes by bin
region (no conflicts, but needs slot→region routing). XPlace uses none of these, so this choice has no
precedent to follow; it should be decided on our URAM budget and on the measured slot order's spatial
locality, not on the GPU.

## Reproduce

```bash
cd ~/phd/Xplace && sed -n 79,118p cpp_to_py/density_map_cuda/density_map_cuda_kernel.cu   # deterministic scatter
cd ~/phd/Xplace && sed -n 183,227p cpp_to_py/density_map_cuda/density_map_cuda_kernel.cu  # deterministic gather
```
The movable/filler split of the scatter time was read from
`.claude/2_ARTIFACTS/xplace_profile/bigblue4/trace.json` by grouping `deterministic_forward` kernel events on
`args.grid[0]` (33 894 → movable, 45 088 → fillers) and averaging `dur` over the 100 launches each.
