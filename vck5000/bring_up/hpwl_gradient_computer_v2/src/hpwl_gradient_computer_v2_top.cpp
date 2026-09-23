// hpwl_gradient_computer_v2_top.cpp -- kernel top wrapping hpwl_gradient_computer_v2 (#41). One
// m_axi bundle per DDR stream so the sequential streams never share a port; scalars on control.

#include "modules/hpwl_gradient_computer_v2.hpp"

using namespace plalgo;

extern "C" void hpwl_gradient_computer_v2_top(
        const pinrec::ChunkDesc*        chunks,
        int                             num_chunks,
        const pinrec::RecordBeat*       records,
        const pinrec::SlotBeat*         pos,
        const pinrec::MacroPinRef*      macro_pins,
        const int32_t*                  import_slots,
        const int32_t*                  export_slots,
        const pinrec::ExchangeBlockRef* blocks,
        float*                          exchange,
        const float*                    offset_table,
        int                             offset_table_size,
        const float*                    exp_lut,
        int                             lut_size,
        OutBeat*                        out_beats,
        pinrec::SlotBeat*               grad,
        int                             offset_bits,
        float                           inv_gamma,
        float                           inv_lut_step) {
#pragma HLS INTERFACE m_axi port=chunks       bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=records      bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=pos          bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=macro_pins   bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=import_slots bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=export_slots bundle=gmem5 offset=slave
#pragma HLS INTERFACE m_axi port=blocks       bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=exchange     bundle=gmem6 offset=slave
#pragma HLS INTERFACE m_axi port=offset_table bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=exp_lut      bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=out_beats    bundle=gmem7 offset=slave
#pragma HLS INTERFACE m_axi port=grad         bundle=gmem8 offset=slave
#pragma HLS INTERFACE s_axilite port=return   bundle=control

    hpwl_gradient_computer_v2(chunks, num_chunks, records, pos, macro_pins, import_slots, export_slots, blocks,
                              exchange, offset_table, offset_table_size, exp_lut, lut_size, out_beats, grad,
                              offset_bits, inv_gamma, inv_lut_step);
}
