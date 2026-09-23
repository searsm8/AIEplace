// hpwl_computer_v2_top.cpp -- kernel top wrapping hpwl_computer_v2 (#41). One m_axi bundle per
// DDR stream so the sequential streams never share a port; scalars on the control bundle.

#include "modules/hpwl_computer_v2.hpp"

using namespace plalgo;

extern "C" void hpwl_computer_v2_top(
        const pinrec::RecordBeat*  records,
        int                        num_beats,
        const int*                 beat_count,
        const pinrec::SlotBeat*    pos,
        int                        num_slot_beats,
        const pinrec::MacroPinRef* macro_pins,
        int                        num_macro_pins,
        const float*               offset_table,
        int                        offset_table_size,
        OutBeat*                   out_beats,
        int                        offset_bits) {
#pragma HLS INTERFACE m_axi port=records      bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=beat_count   bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=pos          bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=macro_pins   bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=offset_table bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=out_beats    bundle=gmem5 offset=slave
#pragma HLS INTERFACE s_axilite port=return   bundle=control

    hpwl_computer_v2(records, num_beats, beat_count, pos, num_slot_beats, macro_pins, num_macro_pins,
                     offset_table, offset_table_size, out_beats, offset_bits);
}
