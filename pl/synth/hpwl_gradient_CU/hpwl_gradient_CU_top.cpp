// hpwl_gradient_CU_top.cpp -- standalone kernel top for the module's C-synthesis / co-sim checks
// (#45). One m_axi bundle per sequential DDR stream; the three small 32-bit tables share gmem9,
// pinned at 32 bits (wider, HLS loses their bursts). Scalars on control.
// From vck5000/bring_up/hpwl_gradient_computer_v2/src/hpwl_gradient_computer_v2_top.cpp @ 41f2193. Meow.

#include "modules/hpwl_gradient_CU.hpp"

using namespace plalgo;

extern "C" void hpwl_gradient_CU_top(
        const pinrec::ChunkDesc*        chunks,
        const int32_t*                  group_ends,
        int                             num_chunks,
        const pinrec::RecordBeat*       records,
        const pinrec::SlotBeat*         pos,             // borrowed rows already sent by iteration_update_CU
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
#pragma HLS INTERFACE m_axi port=chunks         bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=group_ends     bundle=gmem9 offset=slave max_widen_bitwidth=32
#pragma HLS INTERFACE m_axi port=records        bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=pos            bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=macro_beats    bundle=gmem3 offset=slave
// gmem2: refresh reads it while pos is idle, so the two never compete for the port. Meow.
#pragma HLS INTERFACE m_axi port=macro_offsets  bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=offset_table   bundle=gmem9 offset=slave max_widen_bitwidth=32
#pragma HLS INTERFACE m_axi port=exp_lut        bundle=gmem9 offset=slave max_widen_bitwidth=32
// m_axi, not s_axilite: a pointer on s_axilite becomes a write-only control register that XRT cannot
// read back. Own bundle: 64 bits on gmem9 would cost its 32-bit tables their bursts. Meow.
#pragma HLS INTERFACE m_axi port=hpwl_total_fixed bundle=gmem5 offset=slave
#pragma HLS INTERFACE m_axi port=grad           bundle=gmem8 offset=slave

    hpwl_gradient_CU(chunks, group_ends, num_chunks, records, pos, macro_beats, macro_offsets, offset_table, offset_table_size, exp_lut, lut_size, grad,
                     offset_bits, inv_gamma, inv_lut_step, *hpwl_total_fixed);
}
