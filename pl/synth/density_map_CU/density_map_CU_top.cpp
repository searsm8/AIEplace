// density_map_CU_top.cpp -- standalone kernel top for density_map_CU's C-synthesis check (#48). One
// m_axi bundle per sequential DDR stream; scalars and the overflow out on control. Meow.

#include "modules/density_map_CU.hpp"

using namespace plalgo;

extern "C" void density_map_CU_top(
        const pinrec::SlotBeat*        pos_x,
        const pinrec::SlotBeat*        pos_y,
        const densrec::FootprintBeat*  footprint_idx,
        int                            num_slot_beats,
        int                            num_cell_beats,
        const densrec::Footprint*      footprints,
        int                            num_footprints,
        const pinrec::SlotBeat*        fixed_base,
        int                            bins,
        float                          bin_w,
        float                          bin_h,
        float                          inv_bin_area,
        float                          overflow_cap,
        float                          overflow_norm,
        pinrec::SlotBeat*              rho,
        float*                         overflow) {
#pragma HLS INTERFACE m_axi port=pos_x         bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=pos_y         bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=footprint_idx bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=footprints    bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=fixed_base    bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=rho           bundle=gmem5 offset=slave
#pragma HLS INTERFACE s_axilite port=overflow

    density_map_CU(pos_x, pos_y, footprint_idx, num_slot_beats, num_cell_beats, footprints, num_footprints, fixed_base,
                   bins, bin_w, bin_h, inv_bin_area, overflow_cap, overflow_norm, rho, *overflow);
}
