// bin_density_CU_top.cpp -- standalone kernel top for bin_density_CU's C-synthesis check (#48). One
// m_axi bundle per sequential DDR stream; scalars and the overflow excess on control. Meow.

#include "modules/bin_density_CU.hpp"

using namespace plalgo;

extern "C" void bin_density_CU_top(
        const pinrec::SlotBeat*        pos_x,
        const pinrec::SlotBeat*        pos_y,
        const densrec::FootprintBeat*  footprint_idx,
        int                            num_slot_beats,
        int                            num_cell_beats,
        const densrec::SlowSlot*       slow_slots,
        int                            num_slow_cells,
        int                            num_slow,
        const densrec::Footprint*      footprints,
        int                            num_footprints,
        const densrec::FixedBeat*      fixed_base,
        int                            grid_bins,         // not "bins": a SystemVerilog keyword, which breaks co-sim. Meow.
        float                          bin_w,
        float                          bin_h,
        float                          fixed_scale,
        float                          rho_unit,
        unsigned int                   overflow_cap,
        pinrec::SlotBeat*              rho,
        unsigned long long*            overflow_excess) {
#pragma HLS INTERFACE m_axi port=pos_x         bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=pos_y         bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=footprint_idx bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=footprints    bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=fixed_base    bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=rho           bundle=gmem5 offset=slave
#pragma HLS INTERFACE m_axi port=slow_slots    bundle=gmem6 offset=slave
#pragma HLS INTERFACE s_axilite port=overflow_excess

    uint64_t excess = 0;
    bin_density_CU(pos_x, pos_y, footprint_idx, num_slot_beats, num_cell_beats, slow_slots, num_slow_cells, num_slow,
                   footprints, num_footprints, fixed_base, grid_bins, bin_w, bin_h, fixed_scale, rho_unit, overflow_cap, rho, excess);
    *overflow_excess = excess;
}
