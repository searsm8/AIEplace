#ifndef PL_BIN_DENSITY_CU_HPP
#define PL_BIN_DENSITY_CU_HPP

// bin_density_CU -- the bin density map rho (the ePlace charge density, XPlace's density_map) from the
// slot-ordered positions: every OWNED and FILLER slot deposits its clamped footprint's overlap area
// into the bins it covers, on top of the host's capped fixed-node base map; then rho = overlap / bin
// area. Same math as sw_flat Placer::deposit_fixed_point (density.cpp, [sw_flat] deposit = "fixed").
//
// FIXED POINT (#48): each bin is one URAM_WORD_WIDTH (36) word counting 2^-F bin areas, F chosen per
// design by the host from the total-area bound (densrec::rho_frac_bits), so no bin can overflow. Each
// overlap is computed in float exactly as sw_flat computes it, with the footprint weight pre-scaled by
// fixed_scale = 2^F / bin_area, and rounded once (densrec::to_fixed). The adds are integer, so the map
// and the overflow excess do not depend on the deposit order.
//
// BANKS (#48, 2026-10-20): the grid is split over 16 banks. 
// For a node area that lands in bin@(row, col), the bin accumulation occurs in bank = 4 * (col % 4) + (row % 4), 
// address (col / 4, row / 4). Any window of up to 4 x 4 bins then touches 16 different banks, so a footprint of
// at most WINDOW_BINS bins per axis is deposited in ONE cycle: every bank does at most one
// read-add-write. Each bank is a (bins/4)^2 = 64K x 36 b URAM array at 1024: 8 URAMs, 128 in all.
//
// FORWARDING: a bank's read-add-write takes several pipeline stages (the URAM read latency, the add,
// the write). The next cell, one cycle later, may hit the same bin before the first sum is written, and
// would read a stale value. Each bank keeps its last FORWARD_DEPTH writes (address, new value) in
// registers; a read whose address matches one takes that newest value instead of the URAM's. The
// DEPENDENCE pragma tells HLS the URAM need not wait, which is only correct because of this. C
// simulation is sequential, so it cannot tell whether FORWARD_DEPTH covers the real read-to-write
// distance; RTL co-simulation can (pl/synth/bin_density_CU/cosim).
//
// Footprints that may span more than WINDOW_BINS bins (macros, very large cells) are NO_DEPOSIT in the
// stream and arrive on a separate list (slow_slots_DDR), deposited one bin at a time. Order does not
// matter, so the only constraint is the overflow barrier: cells (fast, then slow) before it, fillers after.
//
//   load base       rho_URAM <- fixed_base_DDR (fixed units, capped by the host)
//   load table      footprint_BRAM <- footprints_DDR, weight *= fixed_scale
//   deposit cells   fast stream, then the slow cells
//   overflow        excess = sum max(0, rho - cap), one 4x4 tile (16 banks) per cycle, exact (#48 B')
//   deposit fillers fast stream, then the slow fillers
//   drain           rho_DDR <- (float)rho_URAM * rho_unit
//
// The device returns the integer excess; the host forms the ratio (sw_flat Placer::overflow_from_excess),
// as hpwl_CU returns its int64 HPWL. Grid layout to and from DDR: col-major, bin = col * bins + row,
// 16 per beat (to be revisited with the FFT's input format). Meow.

#include "format/density_footprint.hpp"

#ifdef __SYNTHESIS__
#include <ap_int.h>
#endif

#ifndef BIN_FORWARD_DEPTH
// >= the deposit's scheduled read-to-write distance: 5 (URAM read issued at stage 99, write at 104 of 104,
// csynth 2026-10-27 with the bin index by multiply; the dividers sat before the read, so dropping them took
// 13 stages off the front and left the distance at 5). Co-sim on a stream that re-hits bins at distances
// 1..5: depth 0 and 4 FAIL in RTL, 5 and 6 PASS. 6 leaves one spare; re-run cosim/run_depths.sh whenever
// deposit_fast changes. Meow.
#define BIN_FORWARD_DEPTH 6
#endif

namespace plalgo {

#ifndef ONCHIP_ARRAY   // same rule as hpwl_gradient/slot_arrays.hpp: static only off-synthesis
#ifdef __SYNTHESIS__
#define ONCHIP_ARRAY
#else
#define ONCHIP_ARRAY static
#endif
#endif

// One bin's accumulator. Off-synthesis it is a wider integer, so a bin past URAM_WORD_WIDTH is visible
// to the harnesses (bin_density_CU_test, sw_flat bin_density_check) instead of wrapping as the URAM would. Meow.
#ifdef __SYNTHESIS__
typedef ap_uint<densrec::URAM_WORD_WIDTH> rho_word;
#else
typedef uint64_t rho_word;
#endif

constexpr int BANK_SIDE     = densrec::WINDOW_BINS;              // 4: banks per axis
constexpr int NUM_BANKS     = BANK_SIDE * BANK_SIDE;             // 16
constexpr int BANK_WORDS    = (densrec::GRID_MAX / BANK_SIDE) * (densrec::GRID_MAX / BANK_SIDE);   // 64K at 1024
constexpr int FORWARD_DEPTH = BIN_FORWARD_DEPTH;
constexpr int RHO_BEATS_MAX = densrec::GRID_MAX * densrec::GRID_MAX / pinrec::LANES;

inline int bank_of(int col, int row) { return BANK_SIDE * (col % BANK_SIDE) + row % BANK_SIDE; }
inline int address_of(int col, int row, int grid_bins) { return (col / BANK_SIDE) * (grid_bins / BANK_SIDE) + row / BANK_SIDE; }

// A footprint's corner bins: Grid::computeNodeOverlaps' index range, clipped to the grid. By multiply,
// as sw_flat's bin_index = "multiply": a float divider per edge cost +7.8K LUT per producer (#48). Meow.
struct Window { float xl, yl, xh, yh; int col_lo, col_hi, row_lo, row_hi; };

static Window footprint_window(float x, float y, const densrec::Footprint& fp, int grid_bins, float inv_bin_w, float inv_bin_h) {
#pragma HLS INLINE
    Window w;
    w.xl = x + fp.half_w - fp.half_cw;
    w.yl = y + fp.half_h - fp.half_ch;
    w.xh = w.xl + 2.0f * fp.half_cw;
    w.yh = w.yl + 2.0f * fp.half_ch;
    w.col_lo = (int)(w.xl * inv_bin_w);  if (w.col_lo < 0)        w.col_lo = 0;
    w.col_hi = (int)(w.xh * inv_bin_w);  if (w.col_hi > grid_bins - 1) w.col_hi = grid_bins - 1;
    w.row_lo = (int)(w.yl * inv_bin_h);  if (w.row_lo < 0)        w.row_lo = 0;
    w.row_hi = (int)(w.yh * inv_bin_h);  if (w.row_hi > grid_bins - 1) w.row_hi = grid_bins - 1;
    return w;
}

// The golden's per-column and per-row overlap lengths (sw_flat for_each_overlap), same float expressions. Meow.
static float overlap_x(const Window& w, int col, float bin_w) {
#pragma HLS INLINE
    const float bin_xl = col * bin_w;
    return (w.xh < bin_xl + bin_w ? w.xh : bin_xl + bin_w) - (w.xl > bin_xl ? w.xl : bin_xl);
}
static float overlap_y(const Window& w, int row, float bin_h) {
#pragma HLS INLINE
    const float bin_yl = row * bin_h;
    return (w.yh < bin_yl + bin_h ? w.yh : bin_yl + bin_h) - (w.yl > bin_yl ? w.yl : bin_yl);
}

// Fast path: one slot per cycle over slots [first_slot, last_slot), both beat-aligned. Meow.
static void deposit_fast(const pinrec::SlotBeat* pos_x_DDR, const pinrec::SlotBeat* pos_y_DDR,
                         const densrec::FootprintBeat* footprint_idx_DDR, int first_slot, int last_slot,
                         const densrec::Footprint footprint_BRAM[densrec::FOOTPRINT_TABLE_MAX], int grid_bins,
                         float bin_w, float bin_h, float inv_bin_w, float inv_bin_h, rho_word rho_URAM[NUM_BANKS][BANK_WORDS]) {
    // Per bank, its last FORWARD_DEPTH writes, newest first: the values not yet safe to read back. Meow.
    // (+1: a zero-length array is not C++, and FORWARD_DEPTH = 0 is co-sim's negative control.) Meow.
    int      fwd_addr[NUM_BANKS][FORWARD_DEPTH + 1];
    rho_word fwd_value[NUM_BANKS][FORWARD_DEPTH + 1];
    bool     fwd_valid[NUM_BANKS][FORWARD_DEPTH + 1];
#pragma HLS ARRAY_PARTITION variable=fwd_addr complete dim=0
#pragma HLS ARRAY_PARTITION variable=fwd_value complete dim=0
#pragma HLS ARRAY_PARTITION variable=fwd_valid complete dim=0
    for (int b = 0; b < NUM_BANKS; b++)
        for (int d = 0; d <= FORWARD_DEPTH; d++) fwd_valid[b][d] = false;

    pinrec::SlotBeat x_beat, y_beat;
    densrec::FootprintBeat idx_beat;
deposit_fast_loop:
    for (int s = first_slot; s < last_slot; s++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=rho_URAM inter false   // safe ONLY because of the forwarding registers
        // (1) Fetch: one 512-bit DDR beat holds 16 slots, so x, y and the footprint index are read once
        //     every 16th slot and held; the other 15 slots pick their lane out of the held beats. Meow.
        const int lane = s % pinrec::LANES;
        if (lane == 0) { x_beat = pos_x_DDR[s / pinrec::LANES]; y_beat = pos_y_DDR[s / pinrec::LANES]; idx_beat = footprint_idx_DDR[s / pinrec::LANES]; }
        const uint32_t idx = idx_beat.idx[lane];

        // (2) Footprint and window: look up the clamped size, then the range of bins it covers. A
        //     NO_DEPOSIT slot reads entry 0 so the pipeline never branches; live = false masks it below. Meow.
        const bool live = idx != densrec::NO_DEPOSIT;
        const densrec::Footprint fp = footprint_BRAM[live ? idx : 0];
        const Window w = footprint_window(x_beat.v[lane], y_beat.v[lane], fp, grid_bins, inv_bin_w, inv_bin_h);

        // (3) Overlap lengths per axis: 4 column widths and 4 row heights; a bin's overlap area is then
        //     ow[dc] * oh[dr], 16 products instead of 16 rectangle intersections. Meow.
        // The window's 4 columns and 4 rows: k-th = lo + k, valid up to hi. Column offset k lands in
        // bank column (col_lo + k) % 4, so each bank sees exactly one (column, row) pair. Meow.
        float ow[BANK_SIDE], oh[BANK_SIDE];
#pragma HLS ARRAY_PARTITION variable=ow complete
#pragma HLS ARRAY_PARTITION variable=oh complete
        for (int k = 0; k < BANK_SIDE; k++) {
            ow[k] = w.col_lo + k <= w.col_hi ? overlap_x(w, w.col_lo + k, bin_w) : 0.0f;
            oh[k] = w.row_lo + k <= w.row_hi ? overlap_y(w, w.row_lo + k, bin_h) : 0.0f;
        }

        // Every bank in parallel: which window bin it owns, then read, forward, add, write. Meow.
    banks:
        for (int b = 0; b < NUM_BANKS; b++) {
#pragma HLS UNROLL
            // (4) Which bin this bank owns: the one window column whose col % 4 is this bank's column
            //     (b / 4), and likewise the row. The & 3 wraps the offset into 0..3. Meow.
            const int dc  = (b / BANK_SIDE - w.col_lo) & (BANK_SIDE - 1);   // window column this bank owns
            const int dr  = (b % BANK_SIDE - w.row_lo) & (BANK_SIDE - 1);
            const int col = w.col_lo + dc, row = w.row_lo + dr;

            // (5) hit: the slot is live and the footprint really overlaps that bin. A miss still runs
            //     (6)-(9) at address 0 and simply does not write, so all 16 banks do the same work. Meow.
            const bool hit = live && ow[dc] > 0.0f && oh[dr] > 0.0f;
            const int addr = hit ? address_of(col, row, grid_bins) : 0;   // a miss may lie past the grid edge

            // (6) Read: in hardware this read is issued ~5 stages before (9) writes, so it can miss the
            //     last 5 cells' writes to this bank and return a stale value. Meow.
            rho_word current = rho_URAM[b][addr];

            // (7) Forward: if a register holds a newer value for this address, use it instead. Each
            //     register is a compare (addr ==, and valid) driving a 2:1 mux; the muxes chain from
            //     fwd[DEPTH-1] to fwd[0], so fwd[0], the previous cell's write, decides last. Meow.
            for (int d = FORWARD_DEPTH - 1; d >= 0; d--)   // oldest first, so the newest match wins
                if (fwd_valid[b][d] && fwd_addr[b][d] == addr) current = fwd_value[b][d];

            // (8) Add this cell's deposit: overlap area x pre-scaled weight, rounded once to fixed units. Meow.
            const rho_word updated = current + densrec::to_fixed(ow[dc] * oh[dr] * fp.weight);

            // (9) Write back. This lands in URAM several cycles from now; until then the copy pushed
            //     into fwd[0] in (10) is the only up-to-date one. Meow.
            if (hit) rho_URAM[b][addr] = updated;

            // (10) Shift every register one place older and push this cycle's result into fwd[0]. It
            //      shifts on a miss too (valid = false), so fwd[d] always holds cell m-1-d's write.
            //      (7) -> (8) -> (10) is a loop through registers that must close in one clock; the
            //      read (6) is outside it, which is why it may run early. The top slot, fwd[DEPTH], is
            //      written but never compared: it only exists so DEPTH = 0 still compiles. Meow.
            for (int d = FORWARD_DEPTH; d > 0; d--) {
                fwd_addr[b][d] = fwd_addr[b][d - 1]; fwd_value[b][d] = fwd_value[b][d - 1]; fwd_valid[b][d] = fwd_valid[b][d - 1];
            }
            fwd_addr[b][0] = addr; fwd_value[b][0] = updated; fwd_valid[b][0] = hit;
        }
    }
}

// Slow path: a footprint wider than the window, one bin at a time (no pipelining needed: few slots). Meow.
static void deposit_slow(const pinrec::SlotBeat* pos_x_DDR, const pinrec::SlotBeat* pos_y_DDR,
                         const densrec::SlowSlot* slow_slots_DDR, int first, int last,
                         const densrec::Footprint footprint_BRAM[densrec::FOOTPRINT_TABLE_MAX], int grid_bins,
                         float bin_w, float bin_h, float inv_bin_w, float inv_bin_h, rho_word rho_URAM[NUM_BANKS][BANK_WORDS]) {
    // (1) Per slow slot: fetch its position by slot number and its footprint by table index (the slow
    //     list names both, since the fast stream marked this slot NO_DEPOSIT).
    // (2) Walk every bin of the window, skipping zero overlaps, and add into that bin's bank.
    // No DEPENDENCE pragma here, so HLS keeps each read-add-write in order and no forwarding is needed:
    // correct, just slower, which is fine for the few macros and huge cells that take this path. Meow.
slow_slots:
    for (int i = first; i < last; i++) {
        const densrec::SlowSlot slow = slow_slots_DDR[i];
        const float x = pos_x_DDR[slow.slot / pinrec::LANES].v[slow.slot % pinrec::LANES];
        const float y = pos_y_DDR[slow.slot / pinrec::LANES].v[slow.slot % pinrec::LANES];
        const densrec::Footprint fp = footprint_BRAM[slow.footprint];
        const Window w = footprint_window(x, y, fp, grid_bins, inv_bin_w, inv_bin_h);
    slow_cols:
        for (int col = w.col_lo; col <= w.col_hi; col++) {
            const float ow = overlap_x(w, col, bin_w);
            if (ow <= 0.0f) continue;
        slow_rows:
            for (int row = w.row_lo; row <= w.row_hi; row++) {
                const float oh = overlap_y(w, row, bin_h);
                if (oh <= 0.0f) continue;
                rho_URAM[bank_of(col, row)][address_of(col, row, grid_bins)] += densrec::to_fixed(ow * oh * fp.weight);
            }
        }
    }
}

// XPlace's overflow_fn (run_placement_nesterov.py:41) numerator: sum over bins of max(0, rho - cap), in
// fixed units. One address in every bank is one aligned 4x4 tile: 16 bins per cycle, one per bank.
// Integer adds are exact and one cycle, so a plain tree and one accumulator hold II=1. Meow.
static uint64_t overflow_excess_sum(const rho_word rho_URAM[NUM_BANKS][BANK_WORDS], int grid_bins, uint32_t overflow_cap) {
    uint64_t excess_sum = 0;
    const int tiles = (grid_bins / BANK_SIDE) * (grid_bins / BANK_SIDE);
reduce:
    for (int a = 0; a < tiles; a++) {
#pragma HLS LOOP_TRIPCOUNT max=BANK_WORDS
#pragma HLS PIPELINE II=1
        // (1) Read address a in all 16 banks: the 4x4 tile of bins at tile column a / (bins/4), tile row
        //     a % (bins/4). Each bank gives one bin, so all 16 reads happen in the same cycle.
        // (2) Each bin's excess over the cap, clipped at zero.
        // (3) Adder tree: 16 -> 8 -> 4 -> 2 -> 1, four levels of adds per tile.
        // (4) Add the tile's total to the running sum. Meow.
        uint64_t excess[NUM_BANKS];
#pragma HLS ARRAY_PARTITION variable=excess complete
        for (int b = 0; b < NUM_BANKS; b++) {
            const uint64_t q = (uint64_t)rho_URAM[b][a];
            excess[b] = q > overflow_cap ? q - overflow_cap : 0;
        }
        for (int width = NUM_BANKS / 2; width > 0; width /= 2)   // in place: slot j is read before it is rewritten
            for (int j = 0; j < width; j++) excess[j] = excess[2 * j] + excess[2 * j + 1];
        excess_sum += excess[0];
    }
    return excess_sum;
}

static void bin_density_CU(
        const pinrec::SlotBeat*        pos_x_DDR,        // [num_slot_beats] slot-major, lower-left x, die frame
        const pinrec::SlotBeat*        pos_y_DDR,        // [num_slot_beats] same for y
        const densrec::FootprintBeat*  footprint_idx_DDR, // [num_slot_beats] per slot: table index, or NO_DEPOSIT
                                                         // (nothing to deposit, or a slow slot)
        int                            num_slot_beats,   // the whole slot range: every chunk, then the fillers
        int                            num_cell_beats,   // beats before the filler tail: first_filler_slot / 16
        const densrec::SlowSlot*       slow_slots_DDR,   // [num_slow] slots too wide for the window, ascending
        int                            num_slow_cells,   // slow slots before the filler tail
        int                            num_slow,
        const densrec::Footprint*      footprints_DDR,   // [num_footprints] the distinct footprints, weight unscaled
        int                            num_footprints,   // <= FOOTPRINT_TABLE_MAX
        const densrec::FixedBeat*      fixed_base_DDR,   // [bins*bins/16] fixed overlap, capped, in fixed units
        int                            grid_bins,             // bins per row, <= GRID_MAX, a multiple of 16
        float                          bin_w,            // bin size, die frame
        float                          bin_h,
        float                          inv_bin_w,        // 1.0f / bin_w, as the host's float divide gives it: the bin index
        float                          inv_bin_h,
        float                          fixed_scale,      // 2^F / bin_area[0]: die-frame area -> fixed units
        float                          rho_unit,         // 2^-F: bin areas per fixed unit
        uint32_t                       overflow_cap,     // to_fixed(bin_w * bin_h * td * fixed_scale): XPlace's uniform cap
        pinrec::SlotBeat*              rho_DDR,          // [bins*bins/16] out: bin density, in bin areas
        uint64_t&                      overflow_excess) { // out: fixed base + cells excess, before the fillers
    ONCHIP_ARRAY rho_word rho_URAM[NUM_BANKS][BANK_WORDS];
#pragma HLS ARRAY_PARTITION variable=rho_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=rho_URAM type=ram_2p impl=uram

    ONCHIP_ARRAY densrec::Footprint footprint_BRAM[densrec::FOOTPRINT_TABLE_MAX];

    const int grid_beats = grid_bins * grid_bins / pinrec::LANES;

    // A DDR beat is 16 rows of one column. Rows 4k..4k+3 of a column sit in 4 different banks at one
    // address, so 4 bins move per cycle; 16 per cycle (a 4 x 4 tile, all banks) waits on the DDR
    // format. Meow.
    const int quads = grid_bins * grid_bins / BANK_SIDE;
load_base:
    for (int q = 0; q < quads; q++) {
#pragma HLS LOOP_TRIPCOUNT max=densrec::GRID_MAX*densrec::GRID_MAX/4
#pragma HLS PIPELINE II=1
        // (1) Quad q is 4 consecutive bins of the col-major grid: bin index i = 4q, rows row..row+3 of
        //     one column. (2) Read the DDR beat that holds them. (3) Write each row into its own bank
        //     (bank row k), all at the same address. Meow.
        const int i = q * BANK_SIDE, col = i / grid_bins, row = i % grid_bins;
        const densrec::FixedBeat beat = fixed_base_DDR[i / pinrec::LANES];
        for (int k = 0; k < BANK_SIDE; k++)   // row % 4 == 0, so row + k is bank row k: 4 distinct banks
            rho_URAM[BANK_SIDE * (col % BANK_SIDE) + k][address_of(col, row, grid_bins)] = beat.q[i % pinrec::LANES + k];
    }

    // The scaled weight is sw_flat's fp.weight *= fixed_scale: one float multiply per table entry. Meow.
load_table:
    for (int i = 0; i < num_footprints; i++) {
#pragma HLS LOOP_TRIPCOUNT max=densrec::FOOTPRINT_TABLE_MAX
        densrec::Footprint fp = footprints_DDR[i];
        fp.weight *= fixed_scale;
        footprint_BRAM[i] = fp;
    }

    // Cells first (fast stream, then slow list), then the overflow excess, then the fillers: XPlace's
    // overflow counts cells and fixed nodes but not fillers, so it must be summed between the two. Meow.
    const int first_filler_slot = num_cell_beats * pinrec::LANES;
    deposit_fast(pos_x_DDR, pos_y_DDR, footprint_idx_DDR, 0, first_filler_slot, footprint_BRAM, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, rho_URAM);
    deposit_slow(pos_x_DDR, pos_y_DDR, slow_slots_DDR, 0, num_slow_cells, footprint_BRAM, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, rho_URAM);
    overflow_excess = overflow_excess_sum(rho_URAM, grid_bins, overflow_cap);
    deposit_fast(pos_x_DDR, pos_y_DDR, footprint_idx_DDR, first_filler_slot, num_slot_beats * pinrec::LANES, footprint_BRAM,
                 grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, rho_URAM);
    deposit_slow(pos_x_DDR, pos_y_DDR, slow_slots_DDR, num_slow_cells, num_slow, footprint_BRAM, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, rho_URAM);

    pinrec::SlotBeat out_beat;
drain:
    for (int q = 0; q < quads; q++) {
#pragma HLS LOOP_TRIPCOUNT max=densrec::GRID_MAX*densrec::GRID_MAX/4
#pragma HLS PIPELINE II=1
        // (1) The same quad walk as load_base: 4 rows of one column, one per bank, same address.
        // (2) Convert each to float bin areas (count x 2^-F) into its lane of out_beat.
        // (3) After the 4th quad (lanes 12..15) the beat is full: write it to DDR. Meow.
        const int i = q * BANK_SIDE, col = i / grid_bins, row = i % grid_bins;
        for (int k = 0; k < BANK_SIDE; k++)
            out_beat.v[i % pinrec::LANES + k] = (float)(uint64_t)rho_URAM[BANK_SIDE * (col % BANK_SIDE) + k][address_of(col, row, grid_bins)] * rho_unit;
        if (i % pinrec::LANES == pinrec::LANES - BANK_SIDE) rho_DDR[i / pinrec::LANES] = out_beat;
    }
}

} // namespace plalgo

#endif // PL_BIN_DENSITY_CU_HPP
