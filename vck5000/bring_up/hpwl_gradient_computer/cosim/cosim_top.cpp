// cosim_top.cpp -- hpwl_gradient_computer_top for RTL co-simulation only (#41). Identical to
// ../src/hpwl_gradient_computer_top.cpp except for `depth=` on every m_axi port: co-simulation
// sizes its test-vector buffers from it (without it the C test bench segfaults in the wrapper).
// Depths cover cosim_tb.cpp's synthetic design with margin; the production top needs none. Meow.

#include "modules/hpwl_gradient_computer.hpp"

using namespace plalgo;

extern "C" void hpwl_gradient_computer_top(
        const pinrec::RecordBeat*  records,
        int                        num_beats,
        const int*                 beat_count,
        const int*                 span_count,
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
#pragma HLS INTERFACE m_axi port=records      bundle=gmem0 offset=slave depth=1024
#pragma HLS INTERFACE m_axi port=beat_count   bundle=gmem1 offset=slave depth=15
#pragma HLS INTERFACE m_axi port=span_count   bundle=gmem1 offset=slave depth=7
#pragma HLS INTERFACE m_axi port=pos          bundle=gmem2 offset=slave depth=512
#pragma HLS INTERFACE m_axi port=macro_pins   bundle=gmem3 offset=slave depth=2048
#pragma HLS INTERFACE m_axi port=offset_table bundle=gmem4 offset=slave depth=1024
#pragma HLS INTERFACE m_axi port=exp_lut      bundle=gmem4 offset=slave depth=1024
#pragma HLS INTERFACE m_axi port=out_beats    bundle=gmem5 offset=slave depth=1024
#pragma HLS INTERFACE m_axi port=grad         bundle=gmem6 offset=slave depth=512

    hpwl_gradient_computer(records, num_beats, beat_count, span_count, pos, num_slot_beats, macro_pins, num_macro_pins,
                           offset_table, offset_table_size, exp_lut, lut_size, out_beats, grad,
                           first_fixed_slot, offset_bits, inv_gamma, inv_lut_step);
}
