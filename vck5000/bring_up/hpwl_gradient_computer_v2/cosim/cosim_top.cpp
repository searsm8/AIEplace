// cosim_top.cpp -- hpwl_gradient_computer_v2_top for RTL co-simulation only (#41). Identical to
// ../src/hpwl_gradient_computer_v2_top.cpp except for `depth=` on every m_axi port, which
// co-simulation needs to size its test-vector buffers. Depths cover cosim_tb.cpp's design. Meow.

#include "modules/hpwl_gradient_computer_v2.hpp"

using namespace plalgo;

extern "C" void hpwl_gradient_computer_v2_top(
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
        const float*                    exp_lut,
        int                             lut_size,
        OutBeat*                        out_beats,
        pinrec::SlotBeat*               grad,
        int                             offset_bits,
        float                           inv_gamma,
        float                           inv_lut_step) {
#pragma HLS INTERFACE m_axi port=chunks         bundle=gmem0 offset=slave depth=16
#pragma HLS INTERFACE m_axi port=group_counts   bundle=gmem9 offset=slave max_widen_bitwidth=32 depth=1024
#pragma HLS INTERFACE m_axi port=records        bundle=gmem1 offset=slave depth=1024
#pragma HLS INTERFACE m_axi port=pos            bundle=gmem2 offset=slave depth=1024
#pragma HLS INTERFACE m_axi port=macro_pins     bundle=gmem3 offset=slave depth=2048
#pragma HLS INTERFACE m_axi port=external_slots bundle=gmem4 offset=slave depth=4096
#pragma HLS INTERFACE m_axi port=shared_slots   bundle=gmem5 offset=slave depth=4096
#pragma HLS INTERFACE m_axi port=parcels        bundle=gmem0 offset=slave depth=256
#pragma HLS INTERFACE m_axi port=mailbox        bundle=gmem6 offset=slave depth=4096
#pragma HLS INTERFACE m_axi port=offset_table   bundle=gmem9 offset=slave max_widen_bitwidth=32 depth=1024
#pragma HLS INTERFACE m_axi port=exp_lut        bundle=gmem9 offset=slave max_widen_bitwidth=32 depth=1024
#pragma HLS INTERFACE m_axi port=out_beats      bundle=gmem7 offset=slave depth=1024
#pragma HLS INTERFACE m_axi port=grad           bundle=gmem8 offset=slave depth=1024

    hpwl_gradient_computer_v2(chunks, group_counts, num_chunks, records, pos, macro_pins, external_slots, shared_slots, parcels,
                              mailbox, offset_table, offset_table_size, exp_lut, lut_size, out_beats, grad,
                              offset_bits, inv_gamma, inv_lut_step);
}
