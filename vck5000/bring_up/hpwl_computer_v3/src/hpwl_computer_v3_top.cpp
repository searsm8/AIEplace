// hpwl_computer_v3_top.cpp -- kernel top wrapping hpwl_computer_v3 (#41). One m_axi bundle per
// DDR stream so the sequential streams never share a port; scalars on the control bundle.

#include "modules/hpwl_computer_v3.hpp"

using namespace plalgo;

extern "C" void hpwl_computer_v3_top(
        const pinrec::ChunkDesc*        chunks,
        const int32_t*                  group_counts,
        int                             num_chunks,
        const pinrec::RecordBeat*       records,
        const pinrec::SlotBeat*         pos,
        const pinrec::MacroPinRef*      macro_pins,
        const int32_t*                  external_slots,
        const int32_t*                  shared_slots,
        const pinrec::ParcelRef*        parcels,
        float*                          mailbox,
        const float*                    offset_table,
        int                             offset_table_size,
        OutBeat*                        out_beats,
        int                             offset_bits) {
#pragma HLS INTERFACE m_axi port=chunks         bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=group_counts   bundle=gmem8 offset=slave max_widen_bitwidth=32
#pragma HLS INTERFACE m_axi port=records        bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=pos            bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=macro_pins     bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=external_slots bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=shared_slots   bundle=gmem5 offset=slave
#pragma HLS INTERFACE m_axi port=parcels        bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=mailbox        bundle=gmem6 offset=slave
#pragma HLS INTERFACE m_axi port=offset_table   bundle=gmem8 offset=slave max_widen_bitwidth=32
#pragma HLS INTERFACE m_axi port=out_beats      bundle=gmem7 offset=slave

    hpwl_computer_v3(chunks, group_counts, num_chunks, records, pos, macro_pins, external_slots, shared_slots, parcels, mailbox,
                     offset_table, offset_table_size, out_beats, offset_bits);
}
