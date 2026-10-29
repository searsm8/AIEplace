// cosim_top.cpp -- hpwl_gradient_CU_top for RTL co-simulation only. Identical to
// ../hpwl_gradient_CU_top.cpp except for `depth=` on every m_axi port, which co-simulation needs to
// size its test-vector buffers. Depths cover cosim_tb.cpp's design. Meow.

#include "modules/hpwl_gradient_CU.hpp"

using namespace plalgo;

extern "C" void hpwl_gradient_CU_top(
        const pinrec::ChunkDesc*        chunks,
        const int32_t*                  group_ends,
        int                             num_chunks,
        const pinrec::RecordBeat*       records,
        const pinrec::SlotBeat*         pos,
        const pinrec::MacroBeat*        macro_beats,
        const pinrec::SlotBeat*         macro_offsets,
        const float*                    offset_table,
        int                             offset_table_size,
        const float*                    exp_lut,
        int                             lut_size,
        pinrec::SlotBeat*               grad,
        int                             offset_bits,
        float                           inv_gamma,
        float                           inv_lut_step,
        int64_t*                        hpwl_total_fixed) {
#pragma HLS INTERFACE m_axi port=chunks         bundle=gmem0 offset=slave depth=64
#pragma HLS INTERFACE m_axi port=group_ends     bundle=gmem9 offset=slave max_widen_bitwidth=32 depth=2048
#pragma HLS INTERFACE m_axi port=records        bundle=gmem1 offset=slave depth=2048
#pragma HLS INTERFACE m_axi port=pos            bundle=gmem2 offset=slave depth=4096
#pragma HLS INTERFACE m_axi port=macro_beats    bundle=gmem3 offset=slave depth=4096
#pragma HLS INTERFACE m_axi port=macro_offsets  bundle=gmem2 offset=slave depth=4096
#pragma HLS INTERFACE m_axi port=offset_table   bundle=gmem9 offset=slave max_widen_bitwidth=32 depth=1024
#pragma HLS INTERFACE m_axi port=exp_lut        bundle=gmem9 offset=slave max_widen_bitwidth=32 depth=1024
#pragma HLS INTERFACE s_axilite port=hpwl_total_fixed
#pragma HLS INTERFACE m_axi port=grad           bundle=gmem8 offset=slave depth=4096

    hpwl_gradient_CU(chunks, group_ends, num_chunks, records, pos, macro_beats, macro_offsets, offset_table,
                     offset_table_size, exp_lut, lut_size, grad, offset_bits, inv_gamma, inv_lut_step, *hpwl_total_fixed);
}
