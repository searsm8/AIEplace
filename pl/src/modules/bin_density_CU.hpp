#ifndef PL_BIN_DENSITY_CU_HPP
#define PL_BIN_DENSITY_CU_HPP

// bin_density_CU -- the bin density map rho (the ePlace charge density, XPlace's density_map) from the
// slot-ordered positions: every OWNED and FILLER slot deposits its clamped footprint's overlap area
// into the bins it covers, on top of the host's capped fixed-node base map; then rho = overlap / bin
// area. Same math as sw_flat Placer::deposit_fixed_point (density.cpp, [sw_flat] deposit = "fixed").
//
// FIXED POINT (#48, Mark 2026-10-19): each bin is one RHO_WORD_BITS (36) word counting 2^-RHO_FRAC_BITS
// (18) bin areas -- one URAM word in 8K x 36 mode, so 1024^2 bins = 128 URAMs. Each overlap is computed
// in float exactly as sw_flat computes it, with the footprint weight pre-scaled by fixed_scale, and
// rounded once (densrec::to_fixed). The adds are integer, so the map and the overflow excess do not
// depend on the deposit order: any number of lanes, any banking, still bit-equal to sw_flat.
//
// PROTOTYPE BODY: the contract and the arithmetic are the deliverable; the loop is still one slot at a
// time. Next: 4x4-interleaved banking so a cell's <=3x3 window lands in one cycle, multiple lanes, a
// slow path for macros, and a reciprocal for the bin-index divide.
//
//   load base       rho_URAM <- fixed_base_DDR (fixed units, capped by the host)
//   load table      footprint_BRAM <- footprints_DDR, weight *= fixed_scale
//   deposit cells   every slot before the filler tail: += to_fixed(overlap * weight) over its bins
//   overflow        read-only pass: excess = sum max(0, rho - cap), exact (#48 B')
//   deposit fillers the filler tail, on top
//   drain           rho_DDR <- (float)rho_URAM * RHO_UNIT
//
// The overflow is reduced from the solver's own map between cells and fillers, as XPlace reduces
// mov_density_map before adding the filler map (#48 option B', REPORT_48_overflow_map_20261015.md).
// The device returns the integer excess; the host forms the ratio (sw_flat Placer::overflow_from_excess),
// as hpwl_CU returns its int64 HPWL. Meow.
//
// Grid layout everywhere: col-major, bin = col * bins + row (col = x-bin), as sw_flat's bin maps,
// packed 16 per beat. Whether that is also the FFT's input layout is open with Geert. Meow.

#include "format/density_footprint.hpp"

#ifdef __SYNTHESIS__
#include <ap_int.h>
#endif

namespace plalgo {

#ifndef ONCHIP_ARRAY   // same rule as hpwl_gradient/slot_arrays.hpp: static only off-synthesis
#ifdef __SYNTHESIS__
#define ONCHIP_ARRAY
#else
#define ONCHIP_ARRAY static
#endif
#endif

// One bin's accumulator. Off-synthesis it is a wider integer, so a bin past RHO_WORD_BITS is visible
// to the harnesses (bin_density_CU_test, sw_flat bin_density_check) instead of wrapping as the URAM would. Meow.
#ifdef __SYNTHESIS__
typedef ap_uint<densrec::RHO_WORD_BITS> rho_word;
#else
typedef uint64_t rho_word;
#endif

constexpr int RHO_BEATS_MAX = densrec::GRID_MAX * densrec::GRID_MAX / pinrec::LANES;

// Grid::computeNodeOverlaps' loop, in its order (column, then row); the weight is pre-scaled. Meow.
static void deposit_footprint(float x, float y, const densrec::Footprint& fp, int bins, float bin_w,
                              float bin_h, rho_word rho_URAM[densrec::GRID_MAX * densrec::GRID_MAX]) {
    const float xl = x + fp.half_w - fp.half_cw;
    const float yl = y + fp.half_h - fp.half_ch;
    const float xh = xl + 2.0f * fp.half_cw;
    const float yh = yl + 2.0f * fp.half_ch;
    int col_lo = (int)(xl / bin_w);  if (col_lo < 0)        col_lo = 0;
    int col_hi = (int)(xh / bin_w);  if (col_hi > bins - 1) col_hi = bins - 1;
    int row_lo = (int)(yl / bin_h);  if (row_lo < 0)        row_lo = 0;
    int row_hi = (int)(yh / bin_h);  if (row_hi > bins - 1) row_hi = bins - 1;
cols:
    for (int col = col_lo; col <= col_hi; col++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=3 avg=2   // std cells and fillers; macros span more
        const float bin_xl = col * bin_w;
        const float overlap_w = (xh < bin_xl + bin_w ? xh : bin_xl + bin_w) - (xl > bin_xl ? xl : bin_xl);
        if (overlap_w <= 0.0f) continue;
    rows:
        for (int row = row_lo; row <= row_hi; row++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=3 avg=2
            const float bin_yl = row * bin_h;
            const float overlap_h = (yh < bin_yl + bin_h ? yh : bin_yl + bin_h) - (yl > bin_yl ? yl : bin_yl);
            if (overlap_h <= 0.0f) continue;
            rho_URAM[col * bins + row] += densrec::to_fixed(overlap_w * overlap_h * fp.weight);
        }
    }
}

// One slot at a time over beats [first_beat, last_beat), in slot order. Meow.
static void deposit_slot_beats(const pinrec::SlotBeat* pos_x_DDR, const pinrec::SlotBeat* pos_y_DDR,
                               const densrec::FootprintBeat* footprint_idx_DDR, int first_beat, int last_beat,
                               const densrec::Footprint footprint_BRAM[densrec::FOOTPRINT_TABLE_MAX], int bins,
                               float bin_w, float bin_h, rho_word rho_URAM[densrec::GRID_MAX * densrec::GRID_MAX]) {
deposit:
    for (int b = first_beat; b < last_beat; b++) {
        const pinrec::SlotBeat       x_beat   = pos_x_DDR[b];
        const pinrec::SlotBeat       y_beat   = pos_y_DDR[b];
        const densrec::FootprintBeat idx_beat = footprint_idx_DDR[b];
        for (int j = 0; j < pinrec::LANES; j++) {
            if (idx_beat.idx[j] == densrec::NO_DEPOSIT) continue;
            deposit_footprint(x_beat.v[j], y_beat.v[j], footprint_BRAM[idx_beat.idx[j]], bins, bin_w, bin_h, rho_URAM);
        }
    }
}

// XPlace's overflow_fn (run_placement_nesterov.py:41) numerator: sum over bins of max(0, rho - cap), in
// fixed units. Integer adds are exact and one cycle, so a beat's 16 excesses go through a plain tree
// and one accumulator holds II=1: no rotating partials, no tolerance. Meow.
static uint64_t overflow_excess_sum(const rho_word rho_URAM[densrec::GRID_MAX * densrec::GRID_MAX], int grid_beats,
                                    uint32_t overflow_cap) {
    uint64_t excess_sum = 0;
reduce:
    for (int b = 0; b < grid_beats; b++) {
#pragma HLS LOOP_TRIPCOUNT max=RHO_BEATS_MAX
#pragma HLS PIPELINE II=1   // 16 bins per cycle: HLS infers rho_URAM cyclic-16 from this
        uint64_t excess[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=excess complete
        for (int j = 0; j < pinrec::LANES; j++) {
            const uint64_t q = (uint64_t)rho_URAM[b * pinrec::LANES + j];
            excess[j] = q > overflow_cap ? q - overflow_cap : 0;
        }
        for (int width = pinrec::LANES / 2; width > 0; width /= 2)   // in place: slot j is read before it is rewritten
            for (int j = 0; j < width; j++) excess[j] = excess[2 * j] + excess[2 * j + 1];
        excess_sum += excess[0];
    }
    return excess_sum;
}

static void bin_density_CU(
        const pinrec::SlotBeat*        pos_x_DDR,        // [num_slot_beats] slot-major, lower-left x, die frame
        const pinrec::SlotBeat*        pos_y_DDR,        // [num_slot_beats] same for y
        const densrec::FootprintBeat*  footprint_idx_DDR, // [num_slot_beats] per slot: table index or NO_DEPOSIT
        int                            num_slot_beats,   // the whole slot range: every chunk, then the fillers
        int                            num_cell_beats,   // beats before the filler tail: first_filler_slot / 16
                                                         // (every chunk's num_slots is a multiple of BANKS, so aligned)
        const densrec::Footprint*      footprints_DDR,   // [num_footprints] the distinct footprints, weight unscaled
        int                            num_footprints,   // <= FOOTPRINT_TABLE_MAX
        const densrec::FixedBeat*      fixed_base_DDR,   // [bins*bins/16] fixed overlap, capped, in fixed units
        int                            bins,             // bins per row, <= GRID_MAX, a multiple of 16
        float                          bin_w,            // bin size, die frame
        float                          bin_h,
        float                          fixed_scale,      // 2^RHO_FRAC_BITS / bin_area[0]: die-frame area -> fixed units
        uint32_t                       overflow_cap,     // to_fixed(bin_w * bin_h * td * fixed_scale): XPlace's uniform cap
        pinrec::SlotBeat*              rho_DDR,          // [bins*bins/16] out: bin density, in bin areas
        uint64_t&                      overflow_excess) { // out: fixed base + cells excess, before the fillers
    ONCHIP_ARRAY rho_word rho_URAM[densrec::GRID_MAX * densrec::GRID_MAX];
#pragma HLS BIND_STORAGE variable=rho_URAM type=ram_2p impl=uram

    ONCHIP_ARRAY densrec::Footprint footprint_BRAM[densrec::FOOTPRINT_TABLE_MAX];

    const int grid_beats = bins * bins / pinrec::LANES;

load_base:
    for (int b = 0; b < grid_beats; b++) {
#pragma HLS LOOP_TRIPCOUNT max=RHO_BEATS_MAX
        const densrec::FixedBeat beat = fixed_base_DDR[b];
        for (int j = 0; j < pinrec::LANES; j++) rho_URAM[b * pinrec::LANES + j] = beat.q[j];
    }

    // The scaled weight is sw_flat's fp.weight *= fixed_scale: one float multiply per table entry. Meow.
load_table:
    for (int i = 0; i < num_footprints; i++) {
#pragma HLS LOOP_TRIPCOUNT max=densrec::FOOTPRINT_TABLE_MAX
        densrec::Footprint fp = footprints_DDR[i];
        fp.weight *= fixed_scale;
        footprint_BRAM[i] = fp;
    }

    deposit_slot_beats(pos_x_DDR, pos_y_DDR, footprint_idx_DDR, 0, num_cell_beats, footprint_BRAM, bins, bin_w, bin_h, rho_URAM);
    overflow_excess = overflow_excess_sum(rho_URAM, grid_beats, overflow_cap);
    deposit_slot_beats(pos_x_DDR, pos_y_DDR, footprint_idx_DDR, num_cell_beats, num_slot_beats, footprint_BRAM, bins, bin_w, bin_h, rho_URAM);

drain:
    for (int b = 0; b < grid_beats; b++) {
#pragma HLS LOOP_TRIPCOUNT max=RHO_BEATS_MAX
        pinrec::SlotBeat beat;
        for (int j = 0; j < pinrec::LANES; j++) beat.v[j] = (float)(uint64_t)rho_URAM[b * pinrec::LANES + j] * densrec::RHO_UNIT;
        rho_DDR[b] = beat;
    }
}

} // namespace plalgo

#endif // PL_BIN_DENSITY_CU_HPP
