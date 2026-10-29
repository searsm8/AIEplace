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
// and the overflow excess do not depend on the deposit order -- which is what lets 8 producers deposit
// in parallel and in any interleaving.
//
// BANKS (#48, folded from pl/synth/bin_density_banked 2026-10-29): 64 URAM banks, 8 x 8 interleaved:
// bin (col, row) lives in bank 8 * (col % 8) + row % 8, address (col / 8) * BANK_PITCH + row / 8. Any
// window of up to 4 x 4 bins hits 16 different banks. Each bank is 16K x 36 b at GRID_MAX 1024: 128
// URAMs in all.
//
// THE FAST DEPOSIT is a dataflow region, run once for the cells and once for the fillers:
//   deal_slots     one 16-slot beat (x, y, footprint index) per cycle; lanes 2p and 2p+1 go to producer p
//   produce x 8    one cell per cycle each: its window's up to 16 fixed-point amounts, each pushed into
//                  the FIFO from this producer to the bank that owns that bin (8 x 64 private FIFOs)
//   drain_banks    every bank takes one update per cycle from the first of its 8 FIFOs that has one and
//                  does its read-add-write, with forwarding (below)
// The region ends when every bank has seen every producer's `last`: that is the overflow barrier.
//
// FORWARDING: a bank's read-add-write spans several pipeline stages, so an update one cycle after
// another to the same word would read a stale value. Each bank keeps its last FORWARD_DEPTH writes
// (address, new value) in registers; a read whose address matches one takes the newest. The DEPENDENCE
// pragma tells HLS the URAM need not wait, which is only correct because of this. C simulation runs the
// processes one after another, so it cannot tell whether FORWARD_DEPTH covers the real read-to-write
// distance; RTL co-simulation can (pl/synth/bin_density_CU/cosim).
//
// Footprints that may span more than WINDOW_BINS bins (macros, very large cells) are NO_DEPOSIT in the
// stream and arrive on a separate list (slow_slots_DDR), deposited one bin at a time.
//
//   load base       rho_URAM <- fixed_base_DDR (fixed units, capped by the host), one beat per cycle
//   deposit cells   fast region over the cell beats, then the slow cells
//   overflow        excess = sum max(0, rho - cap), one 8x8 tile (64 banks) per cycle, exact (#48 B')
//   deposit fillers fast region over the filler beats, then the slow fillers
//   drain           rho_DDR <- (float)rho_URAM * rho_unit, one beat per cycle
// Each phase is the only one touching rho_URAM while it runs; inside the region only drain_banks does.
//
// The device returns the integer excess; the host forms the ratio (sw_flat Placer::overflow_from_excess),
// as hpwl_CU returns its int64 HPWL. Grid layout to and from DDR: col-major, bin = col * bins + row,
// 16 per beat (to be revisited with the FFT's input format). Meow.

#include "format/density_footprint.hpp"

#ifdef __SYNTHESIS__
#include <ap_int.h>
#endif

// The real hls::stream when the Vitis HLS headers are on the include path (HLS, co-sim, banked_check);
// otherwise this queue, so the host g++ builds (pl tier 1, sw_flat check-bin-density) need no Vitis.
// The region's processes run one after another off-synthesis, so an unbounded queue is all a stream has
// to be there. Meow.
#if defined(__SYNTHESIS__) || __has_include(<hls_stream.h>)
#include <hls_stream.h>
#else
#include <cstdio>
#include <cstdlib>
#include <deque>
namespace hls {
template <typename T> class stream {
    std::deque<T> queue;
public:
    void write(const T& value) { queue.push_back(value); }
    T read() {
        if (queue.empty()) { std::fprintf(stderr, "hls::stream shim: read while empty\n"); std::abort(); }
        T value = queue.front();
        queue.pop_front();
        return value;
    }
    bool empty() const { return queue.empty(); }
};
} // namespace hls
#endif

#ifndef BIN_FORWARD_DEPTH
// >= drain_banks' scheduled read-to-write distance, 4: co-sim of this CU on a stream that re-hits bank
// words across producers, depths 1-3 FAIL, 4-6 PASS (pl/synth/bin_density_CU/cosim/run_depths.sh,
// 2026-10-29; banked_top measured the same). 6 leaves two spare. Re-run it whenever drain_banks changes. Meow.
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

constexpr int WINDOW         = densrec::WINDOW_BINS;           // 4: bins per axis a fast footprint spans
constexpr int PRODUCERS      = 8;
constexpr int SLOTS_PER_DEAL = pinrec::LANES / PRODUCERS;      // 2: slots of a beat each producer gets
constexpr int BANK_SIDE      = 8;                              // banks per axis
constexpr int NUM_BANKS      = BANK_SIDE * BANK_SIDE;          // 64
constexpr int BANK_PITCH     = densrec::GRID_MAX / BANK_SIDE;  // words per bank column, at every grid size
constexpr int BANK_WORDS     = BANK_PITCH * BANK_PITCH;        // 16K at 1024
constexpr int FORWARD_DEPTH  = BIN_FORWARD_DEPTH;
constexpr int BANK_FIFO_DEPTH = 8;
constexpr int SLOT_BEATS_MAX = 1 << 20;                        // tripcount hint only
static_assert(WINDOW <= BANK_SIDE, "a window must land in distinct banks");
static_assert(SLOTS_PER_DEAL * PRODUCERS == pinrec::LANES, "a beat must deal evenly");
static_assert(BANK_WORDS <= 65536, "BankUpdate.addr is 16 bits");
// One beat is 16 rows of one column: rows 16k..16k+7 at address 2k and rows 16k+8..16k+15 at 2k+1 of
// the same 8 banks, so load_base and drain move a beat per cycle with two accesses per bank. Meow.
static_assert(pinrec::LANES == 2 * BANK_SIDE, "a beat is two words of one bank column");

// The pitch is GRID_MAX / 8, not grid_bins / 8: a constant power of two makes the multiply a shift. The
// run-time pitch cost one DSP per bank per producer (#48). A smaller grid leaves words unused, which
// were allocated anyway. Meow.
inline int bank_of(int col, int row) { return BANK_SIDE * (col % BANK_SIDE) + row % BANK_SIDE; }
inline int address_of(int col, int row) { return (col / BANK_SIDE) * BANK_PITCH + row / BANK_SIDE; }

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

struct SlotPair   { float x[SLOTS_PER_DEAL], y[SLOTS_PER_DEAL]; uint32_t footprint[SLOTS_PER_DEAL]; };
struct BankUpdate { uint16_t addr; uint32_t inc; bool last; };   // last: this producer is done

// The region's reader. First the footprint table, scaled (sw_flat's fp.weight *= fixed_scale), to every
// producer's own copy; then one beat of the three slot streams per cycle, dealt 2 slots per producer.
// The dealing is by lane, so a producer's share of real cells depends on the slot order; accepted
// (#48, 2026-10-29), to be measured, not modelled. Meow.
static void deal_slots(const pinrec::SlotBeat* pos_x_DDR, const pinrec::SlotBeat* pos_y_DDR,
                       const densrec::FootprintBeat* footprint_idx_DDR, int first_beat, int last_beat,
                       const densrec::Footprint* footprints_DDR, int num_footprints, float fixed_scale,
                       hls::stream<densrec::Footprint> table_out[PRODUCERS], hls::stream<SlotPair> slots_out[PRODUCERS]) {
deal_table:
    for (int i = 0; i < num_footprints; i++) {
#pragma HLS LOOP_TRIPCOUNT max=densrec::FOOTPRINT_TABLE_MAX
#pragma HLS PIPELINE II=1
        densrec::Footprint fp = footprints_DDR[i];
        fp.weight *= fixed_scale;
        for (int p = 0; p < PRODUCERS; p++) table_out[p].write(fp);
    }
deal_beats:
    for (int beat = first_beat; beat < last_beat; beat++) {
#pragma HLS LOOP_TRIPCOUNT max=SLOT_BEATS_MAX
#pragma HLS PIPELINE II=1
        // (1) One 512-bit read on each of the three ports: 16 slots' x, y and footprint index.
        // (2) Producer p takes lanes 2p and 2p+1 as one FIFO word, so it reads its FIFO every other
        //     cycle and the reader one beat per cycle keeps all 8 busy. Meow.
        const pinrec::SlotBeat x_beat = pos_x_DDR[beat], y_beat = pos_y_DDR[beat];
        const densrec::FootprintBeat idx_beat = footprint_idx_DDR[beat];
        for (int p = 0; p < PRODUCERS; p++) {
            SlotPair pair;
            for (int k = 0; k < SLOTS_PER_DEAL; k++) {
                pair.x[k] = x_beat.v[SLOTS_PER_DEAL * p + k];
                pair.y[k] = y_beat.v[SLOTS_PER_DEAL * p + k];
                pair.footprint[k] = idx_beat.idx[SLOTS_PER_DEAL * p + k];
            }
            slots_out[p].write(pair);
        }
    }
}

// One producer: its footprint table, then one cell per cycle; every bank that owns a bin of the cell's
// window gets that bin's amount. Same arithmetic as pl/synth/bin_density_producer/producer.hpp. Meow.
static void produce(hls::stream<densrec::Footprint>& table_in, int num_footprints, hls::stream<SlotPair>& slots_in,
                    int num_beats, int grid_bins, float bin_w, float bin_h, float inv_bin_w, float inv_bin_h,
                    hls::stream<BankUpdate> to_bank[NUM_BANKS]) {
    densrec::Footprint table_BRAM[densrec::FOOTPRINT_TABLE_MAX];
load_table:
    for (int i = 0; i < num_footprints; i++) {
#pragma HLS LOOP_TRIPCOUNT max=densrec::FOOTPRINT_TABLE_MAX
#pragma HLS PIPELINE II=1
        table_BRAM[i] = table_in.read();
    }
    SlotPair pair;
produce_cells:
    for (int s = 0; s < SLOTS_PER_DEAL * num_beats; s++) {
#pragma HLS LOOP_TRIPCOUNT max=SLOTS_PER_DEAL*SLOT_BEATS_MAX
#pragma HLS PIPELINE II=1
        // (1) A new pair every other cycle; k picks this cycle's slot of it.
        const int k = s % SLOTS_PER_DEAL;
        if (k == 0) pair = slots_in.read();

        // (2) Footprint and window. A NO_DEPOSIT slot (nothing to deposit, or a slow slot) reads entry 0
        //     so the pipeline never branches; live = false masks it below. Meow.
        const uint32_t idx = pair.footprint[k];
        const bool live = idx != densrec::NO_DEPOSIT;
        const densrec::Footprint fp = table_BRAM[live ? idx : 0];
        const Window w = footprint_window(pair.x[k], pair.y[k], fp, grid_bins, inv_bin_w, inv_bin_h);

        // (3) Overlap lengths per axis, then the 16 amounts in the golden's product order
        //     (ow * oh) * weight: 32 float multiplies, where a producer's DSPs go. Meow.
        float ow[WINDOW], oh[WINDOW];
#pragma HLS ARRAY_PARTITION variable=ow complete
#pragma HLS ARRAY_PARTITION variable=oh complete
        for (int j = 0; j < WINDOW; j++) {
            ow[j] = w.col_lo + j <= w.col_hi ? overlap_x(w, w.col_lo + j, bin_w) : 0.0f;
            oh[j] = w.row_lo + j <= w.row_hi ? overlap_y(w, w.row_lo + j, bin_h) : 0.0f;
        }
        uint32_t inc[WINDOW * WINDOW];
        bool valid[WINDOW * WINDOW];
#pragma HLS ARRAY_PARTITION variable=inc complete
#pragma HLS ARRAY_PARTITION variable=valid complete
        for (int dc = 0; dc < WINDOW; dc++)
            for (int dr = 0; dr < WINDOW; dr++) {
                valid[dc * WINDOW + dr] = live && ow[dc] > 0.0f && oh[dr] > 0.0f;
                inc[dc * WINDOW + dr] = densrec::to_fixed(ow[dc] * oh[dr] * fp.weight);
            }

        // (4) Fill: bank b owns the window column whose col % 8 is b / 8 (and likewise the row), if the
        //     window reaches it; a 16:1 select per bank, then a write into this producer's FIFO to it. Meow.
    fill:
        for (int b = 0; b < NUM_BANKS; b++) {
#pragma HLS UNROLL
            const int dc = (b / BANK_SIDE - w.col_lo) & (BANK_SIDE - 1);
            const int dr = (b % BANK_SIDE - w.row_lo) & (BANK_SIDE - 1);
            if (dc < WINDOW && dr < WINDOW && valid[dc * WINDOW + dr])
                to_bank[b].write(BankUpdate{(uint16_t)address_of(w.col_lo + dc, w.row_lo + dr), inc[dc * WINDOW + dr], false});
        }
    }
done:
    for (int b = 0; b < NUM_BANKS; b++) {
#pragma HLS UNROLL
        to_bank[b].write(BankUpdate{0, 0, true});
    }
}

// The 64 banks: each takes one update per cycle and adds it in, until all 8 producers have said `last`.
// From banked_top.cpp, where Vivado closed it at 2.928 ns (#48). Meow.
static void drain_banks(hls::stream<BankUpdate> from[PRODUCERS][NUM_BANKS], rho_word rho_URAM[NUM_BANKS][BANK_WORDS]) {
    uint8_t  finished[NUM_BANKS];
    uint16_t fwd_addr[NUM_BANKS][FORWARD_DEPTH + 1];   // +1: FORWARD_DEPTH = 0 must still compile
    rho_word fwd_value[NUM_BANKS][FORWARD_DEPTH + 1];
    bool     fwd_valid[NUM_BANKS][FORWARD_DEPTH + 1];
#pragma HLS ARRAY_PARTITION variable=finished complete
#pragma HLS ARRAY_PARTITION variable=fwd_addr complete dim=0
#pragma HLS ARRAY_PARTITION variable=fwd_value complete dim=0
#pragma HLS ARRAY_PARTITION variable=fwd_valid complete dim=0
    for (int b = 0; b < NUM_BANKS; b++) {
#pragma HLS UNROLL
        finished[b] = 0;
        for (int d = 0; d <= FORWARD_DEPTH; d++) fwd_valid[b][d] = false;
    }
    bool done = false, all_finished = false;
drain:
    while (!done) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=rho_URAM inter false   // safe ONLY because of the forwarding registers
        // The exit drives the pipeline's enables (fanout ~14 K), so it must come straight from a register:
        // the 64-way AND over the counts lands in `all_finished` and the exit reads it a cycle later. With
        // the AND on the exit itself, every failing path in Vivado started at finished[]. Two idle cycles. Meow.
        done = all_finished;
        all_finished = true;
        for (int b = 0; b < NUM_BANKS; b++) all_finished &= finished[b] == PRODUCERS;
    banks:
        for (int b = 0; b < NUM_BANKS; b++) {
#pragma HLS UNROLL
            // (1) Fixed priority over this bank's 8 FIFOs as a one-hot `take`; only the taken read is
            //     nonzero, so the update is the OR of all 8: a balanced tree, not the 8-deep mux chain
            //     that `if (pick == p) u = read()` became. Meow.
            BankUpdate u{0, 0, false};
            bool got = false;
            for (int p = 0; p < PRODUCERS; p++) {
                const bool take = !got && !from[p][b].empty();
                got |= take;
                const BankUpdate r = take ? from[p][b].read() : BankUpdate{0, 0, false};
                u.addr |= r.addr; u.inc |= r.inc; u.last |= r.last;
            }
            const bool add = got && !u.last;
            if (got && u.last) finished[b]++;

            // (2) Read, then forward: a register holding a newer value for this address wins, newest last. Meow.
            rho_word current = rho_URAM[b][u.addr];
            for (int d = FORWARD_DEPTH - 1; d >= 0; d--)
                if (fwd_valid[b][d] && fwd_addr[b][d] == u.addr) current = fwd_value[b][d];
            const rho_word updated = current + u.inc;

            // (3) Write back the PREVIOUS cycle's sum (forwarding slot 0), so the store's data is a register
            //     and the URAM read -> select -> add path ends in fwd_value. Every real update is followed
            //     by at least one more cycle (its bank's `last`s come after it), so none is left unwritten. Meow.
            if (fwd_valid[b][0]) rho_URAM[b][fwd_addr[b][0]] = fwd_value[b][0];
            for (int d = FORWARD_DEPTH; d > 0; d--) {
                fwd_addr[b][d] = fwd_addr[b][d - 1]; fwd_value[b][d] = fwd_value[b][d - 1]; fwd_valid[b][d] = fwd_valid[b][d - 1];
            }
            fwd_addr[b][0] = u.addr; fwd_value[b][0] = updated; fwd_valid[b][0] = add;
        }
    }
}

// The fast deposit over beats [first_beat, last_beat). Eight explicit producer calls: a DATAFLOW region
// wants one call per process. Meow.
static void deposit_region(const pinrec::SlotBeat* pos_x_DDR, const pinrec::SlotBeat* pos_y_DDR,
                           const densrec::FootprintBeat* footprint_idx_DDR, int first_beat, int last_beat,
                           const densrec::Footprint* footprints_DDR, int num_footprints, float fixed_scale, int grid_bins,
                           float bin_w, float bin_h, float inv_bin_w, float inv_bin_h, rho_word rho_URAM[NUM_BANKS][BANK_WORDS]) {
#pragma HLS DATAFLOW
    hls::stream<densrec::Footprint> table_FIFO[PRODUCERS];
    hls::stream<SlotPair> slot_FIFO[PRODUCERS];
    hls::stream<BankUpdate> bank_FIFO[PRODUCERS][NUM_BANKS];
#pragma HLS STREAM variable=table_FIFO depth=2
#pragma HLS STREAM variable=slot_FIFO depth=4
#pragma HLS STREAM variable=bank_FIFO depth=BANK_FIFO_DEPTH
    const int num_beats = last_beat - first_beat;
    deal_slots(pos_x_DDR, pos_y_DDR, footprint_idx_DDR, first_beat, last_beat, footprints_DDR, num_footprints, fixed_scale,
               table_FIFO, slot_FIFO);
    produce(table_FIFO[0], num_footprints, slot_FIFO[0], num_beats, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, bank_FIFO[0]);
    produce(table_FIFO[1], num_footprints, slot_FIFO[1], num_beats, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, bank_FIFO[1]);
    produce(table_FIFO[2], num_footprints, slot_FIFO[2], num_beats, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, bank_FIFO[2]);
    produce(table_FIFO[3], num_footprints, slot_FIFO[3], num_beats, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, bank_FIFO[3]);
    produce(table_FIFO[4], num_footprints, slot_FIFO[4], num_beats, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, bank_FIFO[4]);
    produce(table_FIFO[5], num_footprints, slot_FIFO[5], num_beats, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, bank_FIFO[5]);
    produce(table_FIFO[6], num_footprints, slot_FIFO[6], num_beats, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, bank_FIFO[6]);
    produce(table_FIFO[7], num_footprints, slot_FIFO[7], num_beats, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, bank_FIFO[7]);
    drain_banks(bank_FIFO, rho_URAM);
}

// Slow path: a footprint wider than the window, one bin at a time (no pipelining needed: few slots). Meow.
static void deposit_slow(const pinrec::SlotBeat* pos_x_DDR, const pinrec::SlotBeat* pos_y_DDR,
                         const densrec::SlowSlot* slow_slots_DDR, int first, int last,
                         const densrec::Footprint* footprints_DDR, float fixed_scale, int grid_bins,
                         float bin_w, float bin_h, float inv_bin_w, float inv_bin_h, rho_word rho_URAM[NUM_BANKS][BANK_WORDS]) {
    // (1) Per slow slot: fetch its position by slot number and its footprint by table index (the slow
    //     list names both, since the fast stream marked this slot NO_DEPOSIT); scale the weight as
    //     deal_slots does, the same float multiply.
    // (2) Walk every bin of the window, skipping zero overlaps, and add into that bin's bank.
    // No DEPENDENCE pragma here, so HLS keeps each read-add-write in order and no forwarding is needed:
    // correct, just slower, which is fine for the few macros and huge cells that take this path. Meow.
slow_slots:
    for (int i = first; i < last; i++) {
        const densrec::SlowSlot slow = slow_slots_DDR[i];
        const float x = pos_x_DDR[slow.slot / pinrec::LANES].v[slow.slot % pinrec::LANES];
        const float y = pos_y_DDR[slow.slot / pinrec::LANES].v[slow.slot % pinrec::LANES];
        densrec::Footprint fp = footprints_DDR[slow.footprint];
        fp.weight *= fixed_scale;
        const Window w = footprint_window(x, y, fp, grid_bins, inv_bin_w, inv_bin_h);
    slow_cols:
        for (int col = w.col_lo; col <= w.col_hi; col++) {
            const float ow = overlap_x(w, col, bin_w);
            if (ow <= 0.0f) continue;
        slow_rows:
            for (int row = w.row_lo; row <= w.row_hi; row++) {
                const float oh = overlap_y(w, row, bin_h);
                if (oh <= 0.0f) continue;
                rho_URAM[bank_of(col, row)][address_of(col, row)] += densrec::to_fixed(ow * oh * fp.weight);
            }
        }
    }
}

// XPlace's overflow_fn (run_placement_nesterov.py:41) numerator: sum over bins of max(0, rho - cap), in
// fixed units. One address in every bank is one aligned 8x8 tile: 64 bins per cycle, one per bank.
// Integer adds are exact and one cycle, so a plain tree and one accumulator hold II=1. Meow.
static uint64_t overflow_excess_sum(const rho_word rho_URAM[NUM_BANKS][BANK_WORDS], int grid_bins, uint32_t overflow_cap) {
    uint64_t excess_sum = 0;
    const int tiles_per_axis = grid_bins / BANK_SIDE;
    int tile_col = 0, tile_row = 0;   // counters, not a / tiles_per_axis: that would be a run-time divide. Meow.
reduce:
    for (int t = 0; t < tiles_per_axis * tiles_per_axis; t++) {
#pragma HLS LOOP_TRIPCOUNT max=BANK_WORDS
#pragma HLS PIPELINE II=1
        // (1) Read address a in all 64 banks: the 8x8 tile of bins at (tile_col, tile_row). On a grid
        //     below GRID_MAX the words with tile_row >= tiles_per_axis are never visited: never loaded.
        // (2) Each bin's excess over the cap, clipped at zero.
        // (3) Adder tree: 64 -> 32 -> ... -> 1, six levels.
        // (4) Add the tile's total to the running sum. Meow.
        uint64_t excess[NUM_BANKS];
#pragma HLS ARRAY_PARTITION variable=excess complete
        const int a = tile_col * BANK_PITCH + tile_row;
        if (++tile_row == tiles_per_axis) { tile_row = 0; tile_col++; }
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
        int                            grid_bins,        // bins per row, <= GRID_MAX, a multiple of 16
        float                          bin_w,            // bin size, die frame
        float                          bin_h,
        float                          inv_bin_w,        // 1.0f / bin_w, as the host's float divide gives it: the bin index
        float                          inv_bin_h,
        float                          fixed_scale,      // 2^F / bin_area[0]: die-frame area -> fixed units
        float                          rho_unit,         // 2^-F: bin areas per fixed unit
        uint32_t                       overflow_cap,     // to_fixed(bin_w * bin_h * td * fixed_scale): XPlace's uniform cap
        pinrec::SlotBeat*              rho_DDR,          // [bins*bins/16] out: bin density, in bin areas
        uint64_t&                      overflow_excess) { // out: fixed base + cells excess, before the fillers
    // ram_t2p: URAM is natively true dual port, so load_base and drain take two words per bank per cycle
    // (a whole beat); drain_banks uses one port to read and the other to write. Meow.
    ONCHIP_ARRAY rho_word rho_URAM[NUM_BANKS][BANK_WORDS];
#pragma HLS ARRAY_PARTITION variable=rho_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=rho_URAM type=ram_t2p impl=uram latency=3   // registered read: forwarding covers it

    const int grid_beats = grid_bins * grid_bins / pinrec::LANES;
    const int beats_per_col = grid_bins / pinrec::LANES;

load_base:
    for (int beat = 0, col = 0, k = 0; beat < grid_beats; beat++) {
#pragma HLS LOOP_TRIPCOUNT max=densrec::GRID_MAX*densrec::GRID_MAX/16
#pragma HLS PIPELINE II=1
        // (1) Beat `beat` is rows 16k..16k+15 of column col. (2) Its lanes 0..7 go to address 2k and
        //     lanes 8..15 to 2k+1 of the 8 banks of bank column col % 8. Constant bank indices, each
        //     write enabled only in the selected bank column, so every bank has exactly two ports' worth. Meow.
        const densrec::FixedBeat in = fixed_base_DDR[beat];
        const int addr = (col / BANK_SIDE) * BANK_PITCH + 2 * k;
        for (int bank_col = 0; bank_col < BANK_SIDE; bank_col++)
            for (int bank_row = 0; bank_row < BANK_SIDE; bank_row++)
                if (bank_col == col % BANK_SIDE) {
                    rho_URAM[BANK_SIDE * bank_col + bank_row][addr]     = in.q[bank_row];
                    rho_URAM[BANK_SIDE * bank_col + bank_row][addr + 1] = in.q[BANK_SIDE + bank_row];
                }
        if (++k == beats_per_col) { k = 0; col++; }
    }

    // Cells first (fast region, then slow list), then the overflow excess, then the fillers: XPlace's
    // overflow counts cells and fixed nodes but not fillers, so it must be summed between the two. The
    // region's return is the barrier: every bank has taken every producer's `last`. Meow.
    deposit_region(pos_x_DDR, pos_y_DDR, footprint_idx_DDR, 0, num_cell_beats, footprints_DDR, num_footprints, fixed_scale,
                   grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, rho_URAM);
    deposit_slow(pos_x_DDR, pos_y_DDR, slow_slots_DDR, 0, num_slow_cells, footprints_DDR, fixed_scale, grid_bins, bin_w, bin_h,
                 inv_bin_w, inv_bin_h, rho_URAM);
    overflow_excess = overflow_excess_sum(rho_URAM, grid_bins, overflow_cap);
    deposit_region(pos_x_DDR, pos_y_DDR, footprint_idx_DDR, num_cell_beats, num_slot_beats, footprints_DDR, num_footprints,
                   fixed_scale, grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h, rho_URAM);
    deposit_slow(pos_x_DDR, pos_y_DDR, slow_slots_DDR, num_slow_cells, num_slow, footprints_DDR, fixed_scale, grid_bins, bin_w,
                 bin_h, inv_bin_w, inv_bin_h, rho_URAM);

drain:
    for (int beat = 0, col = 0, k = 0; beat < grid_beats; beat++) {
#pragma HLS LOOP_TRIPCOUNT max=densrec::GRID_MAX*densrec::GRID_MAX/16
#pragma HLS PIPELINE II=1
        // The load_base walk in reverse: read address 2k and 2k+1 in all 64 banks (constant indices, two
        // reads per bank), keep the bank column col % 8, convert each to float bin areas (count x 2^-F). Meow.
        const int addr = (col / BANK_SIDE) * BANK_PITCH + 2 * k;
        rho_word low[BANK_SIDE][BANK_SIDE], high[BANK_SIDE][BANK_SIDE];
#pragma HLS ARRAY_PARTITION variable=low complete dim=0
#pragma HLS ARRAY_PARTITION variable=high complete dim=0
        for (int bank_col = 0; bank_col < BANK_SIDE; bank_col++)
            for (int bank_row = 0; bank_row < BANK_SIDE; bank_row++) {
                low[bank_col][bank_row]  = rho_URAM[BANK_SIDE * bank_col + bank_row][addr];
                high[bank_col][bank_row] = rho_URAM[BANK_SIDE * bank_col + bank_row][addr + 1];
            }
        pinrec::SlotBeat out;
        for (int bank_row = 0; bank_row < BANK_SIDE; bank_row++) {
            out.v[bank_row]             = (float)(uint64_t)low[col % BANK_SIDE][bank_row] * rho_unit;
            out.v[BANK_SIDE + bank_row] = (float)(uint64_t)high[col % BANK_SIDE][bank_row] * rho_unit;
        }
        rho_DDR[beat] = out;
        if (++k == beats_per_col) { k = 0; col++; }
    }
}

} // namespace plalgo

#endif // PL_BIN_DENSITY_CU_HPP
