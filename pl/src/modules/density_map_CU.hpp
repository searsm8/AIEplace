#ifndef PL_DENSITY_MAP_CU_HPP
#define PL_DENSITY_MAP_CU_HPP

// density_map_CU -- the bin density map rho (the ePlace charge density, XPlace's density_map) from the
// slot-ordered positions: every OWNED and FILLER slot deposits its clamped footprint's overlap area
// into the bins it covers, on top of the host's capped fixed-node base map; then rho = overlap / bin
// area. Same math as sw_flat Placer::compute_electric_fields' deposit (density.cpp), which is
// sw_only's Grid::computeOverlaps.
//
// PROTOTYPE SKELETON (2026-10-13): the contract (inputs, outputs, layouts) is the deliverable; the
// body is the plain golden loop, one slot at a time, so the harness has something to pass. Not yet
// synthesized. The optimizations it is the starting point for: 4x4-interleaved banking so a cell's
// <=3x3 window lands in one cycle, a fixed-point accumulator to remove the float RMW hazard,
// multiple lanes, a slow path for macros, and a reciprocal for the bin-index divide.
//
//   load base     rho_URAM <- fixed_base_DDR (bin overlap area, capped)
//   load table    footprint_BRAM <- footprints_DDR
//   deposit       for every slot: x, y, footprint idx -> += overlap * weight over its bins
//   drain         rho_DDR <- rho_URAM * inv_bin_area
//
// Grid layout everywhere: col-major, bin = col * bins + row (col = x-bin), as sw_flat's bin_overlap,
// packed 16 floats per beat. Whether that is also the FFT's input layout is open with Geert. Meow.

#include "format/density_footprint.hpp"

namespace plalgo {

#ifndef ONCHIP_ARRAY   // same rule as hpwl_gradient/slot_arrays.hpp: static only off-synthesis
#ifdef __SYNTHESIS__
#define ONCHIP_ARRAY
#else
#define ONCHIP_ARRAY static
#endif
#endif

constexpr int RHO_BEATS_MAX = densrec::GRID_MAX * densrec::GRID_MAX / pinrec::LANES;

// Grid::computeNodeOverlaps' loop, in its order (column, then row). Meow.
static void deposit_footprint(float x, float y, const densrec::Footprint& fp, int bins, float bin_w,
                              float bin_h, float rho_URAM[densrec::GRID_MAX * densrec::GRID_MAX]) {
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
            rho_URAM[col * bins + row] += overlap_w * overlap_h * fp.weight;
        }
    }
}

static void density_map_CU(
        const pinrec::SlotBeat*        pos_x_DDR,        // [num_slot_beats] slot-major, lower-left x, die frame
        const pinrec::SlotBeat*        pos_y_DDR,        // [num_slot_beats] same for y
        const densrec::FootprintBeat*  footprint_idx_DDR, // [num_slot_beats] per slot: table index or NO_DEPOSIT
        int                            num_slot_beats,   // the whole slot range: every chunk, then the fillers
        const densrec::Footprint*      footprints_DDR,   // [num_footprints] the distinct footprints
        int                            num_footprints,   // <= FOOTPRINT_TABLE_MAX
        const pinrec::SlotBeat*        fixed_base_DDR,   // [bins*bins/16] fixed overlap area, capped at bin_area*td
        int                            bins,             // bins per row, <= GRID_MAX, a multiple of 16
        float                          bin_w,            // bin size, die frame
        float                          bin_h,
        float                          inv_bin_area,     // 1 / bin_area[0], as the golden normalizes
        pinrec::SlotBeat*              rho_DDR) {        // [bins*bins/16] out: bin density
    ONCHIP_ARRAY float rho_URAM[densrec::GRID_MAX * densrec::GRID_MAX];
#pragma HLS BIND_STORAGE variable=rho_URAM type=ram_2p impl=uram

    ONCHIP_ARRAY densrec::Footprint footprint_BRAM[densrec::FOOTPRINT_TABLE_MAX];

    const int grid_beats = bins * bins / pinrec::LANES;

load_base:
    for (int b = 0; b < grid_beats; b++) {
#pragma HLS LOOP_TRIPCOUNT max=RHO_BEATS_MAX
        const pinrec::SlotBeat beat = fixed_base_DDR[b];
        for (int j = 0; j < pinrec::LANES; j++) rho_URAM[b * pinrec::LANES + j] = beat.v[j];
    }

load_table:
    for (int i = 0; i < num_footprints; i++) {
#pragma HLS LOOP_TRIPCOUNT max=densrec::FOOTPRINT_TABLE_MAX
        footprint_BRAM[i] = footprints_DDR[i];
    }

deposit:
    for (int b = 0; b < num_slot_beats; b++) {
        const pinrec::SlotBeat       x_beat   = pos_x_DDR[b];
        const pinrec::SlotBeat       y_beat   = pos_y_DDR[b];
        const densrec::FootprintBeat idx_beat = footprint_idx_DDR[b];
        for (int j = 0; j < pinrec::LANES; j++) {
            if (idx_beat.idx[j] == densrec::NO_DEPOSIT) continue;
            deposit_footprint(x_beat.v[j], y_beat.v[j], footprint_BRAM[idx_beat.idx[j]], bins, bin_w, bin_h, rho_URAM);
        }
    }

drain:
    for (int b = 0; b < grid_beats; b++) {
#pragma HLS LOOP_TRIPCOUNT max=RHO_BEATS_MAX
        pinrec::SlotBeat beat;
        for (int j = 0; j < pinrec::LANES; j++) beat.v[j] = rho_URAM[b * pinrec::LANES + j] * inv_bin_area;
        rho_DDR[b] = beat;
    }
}

} // namespace plalgo

#endif // PL_DENSITY_MAP_CU_HPP
