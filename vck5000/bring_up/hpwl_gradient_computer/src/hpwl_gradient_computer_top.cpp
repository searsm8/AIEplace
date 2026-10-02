// hpwl_gradient_computer_top.cpp -- kernel top wrapping hpwl_gradient_computer (#41). Scalars go on
// the AXI-Lite control port. Meow.

#include "modules/hpwl_gradient_computer.hpp"

using namespace plalgo;

extern "C" void hpwl_gradient_computer_top(
        const pinrec::RecordBeat*  records,
        int                        num_beats,
        const int*                 beat_count,
        const pinrec::SlotBeat*    pos,
        int                        num_slot_beats,
        const pinrec::MacroPinRef* macro_pins,
        int                        num_macro_pins,
        const float*               offset_table,
        int                        offset_table_size,
        const float*               exp_lut,
        int                        lut_size,
        OutBeat*                   out_beats,
        pinrec::SlotBeat*          grad,
        int                        first_fixed_slot,
        int                        offset_bits,
        float                      inv_gamma,
        float                      inv_lut_step) {
// Bundles are grouped by data WIDTH, not by stream: a bundle's port is as wide as its widest pointer,
// and HLS 2022.2 infers no burst for a narrower pointer on it (measured, #41 bundle experiment).
// Streams never overlap, so same-width pointers can share (gmem4). Meow.
#pragma HLS INTERFACE m_axi port=records      bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=beat_count   bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=pos          bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=macro_pins   bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=offset_table bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=exp_lut      bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=out_beats    bundle=gmem5 offset=slave
#pragma HLS INTERFACE m_axi port=grad         bundle=gmem6 offset=slave

    hpwl_gradient_computer(records, num_beats, beat_count, pos, num_slot_beats, macro_pins, num_macro_pins,
                           offset_table, offset_table_size, exp_lut, lut_size, out_beats, grad,
                           first_fixed_slot, offset_bits, inv_gamma, inv_lut_step);
}
