// hpwl_computer_v3_top.cpp -- kernel top wrapping hpwl_computer_v3 (#41). One m_axi bundle per
// DDR stream so the sequential streams never share a port; scalars on the control bundle.

#include "modules/hpwl_computer_v3.hpp"

using namespace plalgo;

extern "C" void hpwl_computer_v3_top(
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
        OutBeat*                        out_beats,
        int                             offset_bits) {
#pragma HLS INTERFACE m_axi port=chunks       bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=records      bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=pos          bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=macro_pins   bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=import_slots bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=export_slots bundle=gmem5 offset=slave
#pragma HLS INTERFACE m_axi port=blocks       bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=exchange     bundle=gmem6 offset=slave
#pragma HLS INTERFACE m_axi port=offset_table bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=out_beats    bundle=gmem7 offset=slave
#pragma HLS INTERFACE s_axilite port=return   bundle=control

    hpwl_computer_v3(chunks, num_chunks, records, pos, macro_pins, import_slots, export_slots, blocks, exchange,
                     offset_table, offset_table_size, out_beats, offset_bits);
}
