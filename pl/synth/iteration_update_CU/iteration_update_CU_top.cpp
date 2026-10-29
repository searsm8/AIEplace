// iteration_update_CU_top.cpp -- standalone kernel top for the module's C-synthesis check (#45 step 6).
// One m_axi bundle per per-slot array, since a pipelined beat reads up to 12 of them in one cycle; the
// small descriptor and op tables share gmem0. Scalars on control. The DDR port budget beside
// hpwl_gradient_CU is not decided (PLAN_47 item 8): this top measures the loops, not the platform. Meow.

#include "modules/iteration_update_CU.hpp"

using namespace plalgo;

extern "C" void iteration_update_CU_top(
        int mode, int num_chunks,
        const pinrec::ChunkDesc* chunks, const pinrec::FusedOwnerDesc* owners, const uint64_t* fused_ops,
        int filler_beat_offset, int num_filler_beats, const uint16_t* movable,
        const pinrec::SlotBeat* force_x, const pinrec::SlotBeat* force_y,
        const pinrec::SlotBeat* cur_node_x, const pinrec::SlotBeat* cur_node_y,
        const pinrec::SlotBeat* cur_probe_x, const pinrec::SlotBeat* cur_probe_y,
        const pinrec::SlotBeat* cur_grad_x, const pinrec::SlotBeat* cur_grad_y,
        pinrec::SlotBeat* next_node_x, pinrec::SlotBeat* next_node_y,
        pinrec::SlotBeat* next_probe_x, pinrec::SlotBeat* next_probe_y,
        pinrec::SlotBeat* next_grad_x, pinrec::SlotBeat* next_grad_y,
        pinrec::SlotBeat* inv_precond,
        const pinrec::SlotBeat* num_pins, const pinrec::SlotBeat* area,
        const pinrec::SlotBeat* width, const pinrec::SlotBeat* height,
        UpdateScalars scalars, float* norms) {
#pragma HLS INTERFACE m_axi port=chunks      bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=owners      bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=fused_ops   bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=movable     bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=force_x     bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=force_y     bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=cur_node_x  bundle=gmem5 offset=slave
#pragma HLS INTERFACE m_axi port=cur_node_y  bundle=gmem6 offset=slave
#pragma HLS INTERFACE m_axi port=cur_probe_x bundle=gmem7 offset=slave
#pragma HLS INTERFACE m_axi port=cur_probe_y bundle=gmem8 offset=slave
#pragma HLS INTERFACE m_axi port=cur_grad_x  bundle=gmem9 offset=slave
#pragma HLS INTERFACE m_axi port=cur_grad_y  bundle=gmem10 offset=slave
#pragma HLS INTERFACE m_axi port=next_node_x bundle=gmem11 offset=slave
#pragma HLS INTERFACE m_axi port=next_node_y bundle=gmem12 offset=slave
#pragma HLS INTERFACE m_axi port=next_probe_x bundle=gmem13 offset=slave
#pragma HLS INTERFACE m_axi port=next_probe_y bundle=gmem14 offset=slave
#pragma HLS INTERFACE m_axi port=next_grad_x bundle=gmem15 offset=slave
#pragma HLS INTERFACE m_axi port=next_grad_y bundle=gmem16 offset=slave
#pragma HLS INTERFACE m_axi port=inv_precond bundle=gmem17 offset=slave
#pragma HLS INTERFACE m_axi port=num_pins    bundle=gmem18 offset=slave
#pragma HLS INTERFACE m_axi port=area        bundle=gmem19 offset=slave
#pragma HLS INTERFACE m_axi port=width       bundle=gmem20 offset=slave
#pragma HLS INTERFACE m_axi port=height      bundle=gmem21 offset=slave
#pragma HLS INTERFACE m_axi port=norms       bundle=gmem22 offset=slave

    iteration_update_CU(mode, num_chunks, chunks, owners, fused_ops, filler_beat_offset, num_filler_beats, movable,
                        force_x, force_y, cur_node_x, cur_node_y, cur_probe_x, cur_probe_y, cur_grad_x, cur_grad_y,
                        next_node_x, next_node_y, next_probe_x, next_probe_y, next_grad_x, next_grad_y, inv_precond,
                        num_pins, area, width, height, scalars, norms);
}
