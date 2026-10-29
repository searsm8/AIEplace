#ifndef PL_ITERATION_UPDATE_CU_HPP
#define PL_ITERATION_UPDATE_CU_HPP

// iteration_update_CU -- sw_flat's Placer::iteration_update (host/sw_flat/placer/placer.cpp) on the PL, in
// slot order, with #47's borrowed exchange folded in (B4 = A). Two calls per iteration, because the BB
// step length needs a sum over every slot before the first slot can step, and the host's schedule sits
// between (PLAN_45_iteration_update_CU_20261029.md):
//
//   UPDATE_COMBINE  merge_borrowed_gradients + combine_gradients + lipschitz_estimate's two sums.
//                   next_grad (owned) = WL gradient + each borrower's gradient for it - density force;
//                   norms_out = |v_next - v|^2, |P^-1 (g_next - g)|^2, |g_wl|_1, |g_density|_1
//   UPDATE_STEP     update_precond_weights + take_step after the host swapped the [current, next]
//                   buffers (that swap IS cache_state). inv_precond, next node / probe positions, and the
//                   new probe positions sent into every borrower's block; norms_out = |a1|_1, |a2|_1
//
// Both stream each chunk's OWNED beats [slot_beat_offset, + first_borrowed_slot / 16), then the filler
// tail, as segments of one loop; a lane is updated only if `movable` says it is OWNED or FILLER (row
// padding is not). The shared beats (below first_private_slot, C8) run beside the owner's fused op list
// (borrowed_exchange.hpp); a beat costs one cycle plus its extra ops. Every sum is slot_reduce.hpp's order,
// which sw_flat's `reductions = "lanes"` reproduces. The density force is an input until a force gather
// produces it (#48). Per-lane arithmetic is sw_flat's expression for expression: bit-exact by construction,
// held so by host/sw_flat/test/iteration_update_check.cpp. Meow.

#include "modules/iteration_update/borrowed_exchange.hpp"
#include "modules/iteration_update/slot_reduce.hpp"
#include "modules/hpwl_gradient/slot_arrays.hpp"   // ONCHIP_ARRAY

#include <cmath>

namespace plalgo {

enum UpdateMode { UPDATE_COMBINE = 0, UPDATE_STEP = 1 };

// Host -> device, UPDATE_STEP only. die_* are sw_flat's int die size as float; clamp_* = bin_* * (float)M_SQRT2,
// or 0 with enable_density_clamp off. lambda_area_coef = precond_coef * density_weight, as sw_flat forms it. Meow.
struct UpdateScalars {
    float step_length, momentum_coeff, lambda_area_coef;
    float die_w, die_h, clamp_w, clamp_h;
    int   precond_enabled;
};

// std::max(a, b) and std::clamp(v, lo, hi) as libstdc++ writes them, so a NaN goes where sw_flat's goes. Meow.
static float std_max(float a, float b) { return a < b ? b : a; }
static float std_clamp(float v, float lo, float hi) { return v < lo ? lo : (hi < v ? hi : v); }

// enforce_die_boundaries' range for a lower-left coordinate, one axis. Meow.
static void die_range(float size, float clamp_size, float die, float& lo, float& hi) {
#pragma HLS INLINE
    const float expanded = std_max(size, clamp_size);
    lo = 0.5f * (expanded - size);
    hi = die - 0.5f * (expanded + size);
    if (lo > hi) lo = hi = 0.5f * (die - size);
}

struct Segment { int base, owned_beats, shared_beats, num_ops, op_offset; bool filler; };

static Segment segment_of(int seg, int num_chunks, const pinrec::ChunkDesc* chunks_DDR, const pinrec::FusedOwnerDesc& own,
                          int filler_beat_offset, int num_filler_beats, bool send) {
#pragma HLS INLINE
    Segment g;
    g.filler = seg == num_chunks;
    if (g.filler) { g.base = filler_beat_offset; g.owned_beats = num_filler_beats; g.shared_beats = 0; g.num_ops = 0; g.op_offset = 0; return g; }
    const pinrec::ChunkDesc desc = chunks_DDR[seg];
    g.base = desc.slot_beat_offset;
    g.owned_beats = desc.first_borrowed_slot / pinrec::LANES;
    g.shared_beats = desc.first_private_slot / pinrec::LANES;
    g.num_ops = send ? own.num_send_ops : own.num_merge_ops;
    g.op_offset = send ? own.send_op_offset : own.merge_op_offset;
    return g;
}

static void reset_exchange(int next_row_REG[MAX_BORROWERS][pinrec::BANKS], int beat_count_REG[MAX_BORROWERS],
                           bool held_REG[MAX_BORROWERS][pinrec::LANES]) {
#pragma HLS INLINE
    for (int s = 0; s < MAX_BORROWERS; s++) {
        beat_count_REG[s] = 0;
        for (int b = 0; b < pinrec::BANKS; b++) next_row_REG[s][b] = 0;
        for (int j = 0; j < pinrec::LANES; j++) held_REG[s][j] = false;
    }
}

static void combine_pass(int num_chunks, const pinrec::ChunkDesc* chunks_DDR, const pinrec::FusedOwnerDesc* owners_DDR,
                         const uint64_t* fused_ops_DDR, int filler_beat_offset, int num_filler_beats,
                         const uint16_t* movable_DDR, const pinrec::SlotBeat* force_x_DDR, const pinrec::SlotBeat* force_y_DDR,
                         const pinrec::SlotBeat* cur_probe_x_DDR, const pinrec::SlotBeat* cur_probe_y_DDR,
                         const pinrec::SlotBeat* cur_grad_x_DDR, const pinrec::SlotBeat* cur_grad_y_DDR,
                         const pinrec::SlotBeat* next_probe_x_DDR, const pinrec::SlotBeat* next_probe_y_DDR,
                         pinrec::SlotBeat* next_grad_x_DDR, pinrec::SlotBeat* next_grad_y_DDR,
                         const pinrec::SlotBeat* inv_precond_DDR,
                         float ring_BRAM[MAX_BORROWERS][2][pinrec::BANKS][RING_ROWS], float norms_out[4]) {
    int next_row_REG[MAX_BORROWERS][pinrec::BANKS];
#pragma HLS ARRAY_PARTITION variable=next_row_REG complete dim=0
    int beat_count_REG[MAX_BORROWERS];
#pragma HLS ARRAY_PARTITION variable=beat_count_REG complete dim=0
    float addend_REG[MAX_BORROWERS][2][pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=addend_REG complete dim=0
    bool held_REG[MAX_BORROWERS][pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=held_REG complete dim=0
    SlotReducer pos_sum, grad_sum, gwl_sum, gden_sum;
#pragma HLS ARRAY_PARTITION variable=pos_sum.partial complete
#pragma HLS ARRAY_PARTITION variable=grad_sum.partial complete
#pragma HLS ARRAY_PARTITION variable=gwl_sum.partial complete
#pragma HLS ARRAY_PARTITION variable=gden_sum.partial complete
    pos_sum.clear(); grad_sum.clear(); gwl_sum.clear(); gden_sum.clear();
    int beat_index = 0;

    // Every chunk's owned beats, then the fillers: merge each owner's borrowed gradients in, subtract the
    // density force, and sum the four norms. Meow.
combine_segments:
    for (int seg = 0; seg <= num_chunks; seg++) {
        const pinrec::FusedOwnerDesc own = seg < num_chunks ? owners_DDR[seg] : pinrec::FusedOwnerDesc{};
        const Segment g = segment_of(seg, num_chunks, chunks_DDR, own, filler_beat_offset, num_filler_beats, false);
        reset_exchange(next_row_REG, beat_count_REG, held_REG);
        int t = 0;
        // One op or one private beat per cycle. A beat is finished at its BOUNDARY op (private beats are
        // all boundary): its owner lanes then hold the WL gradient plus every borrowed one. Meow.
    combine_beats:
        for (int i = 0; i < g.num_ops + g.owned_beats - g.shared_beats; i++) {
#pragma HLS LOOP_TRIPCOUNT max=65536
#pragma HLS PIPELINE II=1
            // An owned beat is written after it is read, and never read again in this call; the blocks
            // read here are borrowed rows, which this call never writes. Meow.
#pragma HLS DEPENDENCE variable=next_grad_x_DDR inter false
#pragma HLS DEPENDENCE variable=next_grad_y_DDR inter false
#pragma HLS DEPENDENCE variable=ring_BRAM inter false
            const FusedOp op = decode_fused_op(i < g.num_ops ? fused_ops_DDR[g.op_offset + i] : pinrec::FUSED_BOUNDARY);
            const int beat_addr = op.beat ? own.block_beat_offset[op.borrower] + beat_count_REG[op.borrower] : g.base + t;
            // One read site per array per cycle: a borrowed block's beat, or the owned beat's WL gradient. Meow.
            pinrec::SlotBeat grad_in[2];
            for (int j = 0; j < pinrec::LANES; j++) { grad_in[0].v[j] = 0.0f; grad_in[1].v[j] = 0.0f; }
            if (op.beat || (op.boundary && !g.filler)) { grad_in[0] = next_grad_x_DDR[beat_addr]; grad_in[1] = next_grad_y_DDR[beat_addr]; }
            if (op.beat) merge_load_beat(op, grad_in, ring_BRAM, beat_count_REG);
            else         merge_group(op, ring_BRAM, next_row_REG, addend_REG, held_REG);
            if (!op.boundary) continue;

            const int addr = g.base + t;
            const uint16_t movable = movable_DDR[addr];
            const pinrec::SlotBeat fx = force_x_DDR[addr], fy = force_y_DDR[addr];
            const pinrec::SlotBeat cpx = cur_probe_x_DDR[addr], cpy = cur_probe_y_DDR[addr];
            const pinrec::SlotBeat cgx = cur_grad_x_DDR[addr], cgy = cur_grad_y_DDR[addr];
            const pinrec::SlotBeat npx = next_probe_x_DDR[addr], npy = next_probe_y_DDR[addr];
            const pinrec::SlotBeat ipw = inv_precond_DDR[addr];
            pinrec::SlotBeat out_x, out_y;
            float pos_terms[pinrec::LANES], grad_terms[pinrec::LANES], gwl_terms[pinrec::LANES], gden_terms[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=pos_terms complete
#pragma HLS ARRAY_PARTITION variable=grad_terms complete
#pragma HLS ARRAY_PARTITION variable=gwl_terms complete
#pragma HLS ARRAY_PARTITION variable=gden_terms complete
            for (int j = 0; j < pinrec::LANES; j++) {
                const bool m = (movable >> j) & 1;
                float add_x[MAX_BORROWERS], add_y[MAX_BORROWERS];
                bool held[MAX_BORROWERS];
                for (int s = 0; s < MAX_BORROWERS; s++) { add_x[s] = addend_REG[s][0][j]; add_y[s] = addend_REG[s][1][j]; held[s] = held_REG[s][j]; }
                const float wl_x = merge_chain(grad_in[0].v[j], add_x, held);   // merge_borrowed_gradients
                const float wl_y = merge_chain(grad_in[1].v[j], add_y, held);
                // combine_gradients: the two L1 terms, then g -= electro. Meow.
                gwl_terms[j]  = m && !g.filler ? fabsf(wl_x) + fabsf(wl_y) : 0.0f;
                gden_terms[j] = m ? fabsf(fx.v[j]) + fabsf(fy.v[j]) : 0.0f;
                const float g_x = wl_x - fx.v[j], g_y = wl_y - fy.v[j];
                out_x.v[j] = m ? g_x : wl_x;
                out_y.v[j] = m ? g_y : wl_y;
                // lipschitz_estimate's terms. Meow.
                const float dx = npx.v[j] - cpx.v[j], dy = npy.v[j] - cpy.v[j];
                pos_terms[j] = m ? dx * dx + dy * dy : 0.0f;
                const float dgx = ipw.v[j] * (g_x - cgx.v[j]), dgy = ipw.v[j] * (g_y - cgy.v[j]);
                grad_terms[j] = m ? dgx * dgx + dgy * dgy : 0.0f;
            }
            next_grad_x_DDR[addr] = out_x;
            next_grad_y_DDR[addr] = out_y;
            pos_sum.add_beat(beat_index, pos_terms);
            grad_sum.add_beat(beat_index, grad_terms);
            gwl_sum.add_beat(beat_index, gwl_terms);
            gden_sum.add_beat(beat_index, gden_terms);
            for (int s = 0; s < MAX_BORROWERS; s++) for (int j = 0; j < pinrec::LANES; j++) held_REG[s][j] = false;
            t++;
            beat_index++;
        }
    }
    norms_out[0] = pos_sum.total();
    norms_out[1] = grad_sum.total();
    norms_out[2] = gwl_sum.total();
    norms_out[3] = gden_sum.total();
}

static void step_pass(int num_chunks, const pinrec::ChunkDesc* chunks_DDR, const pinrec::FusedOwnerDesc* owners_DDR,
                      const uint64_t* fused_ops_DDR, int filler_beat_offset, int num_filler_beats,
                      const uint16_t* movable_DDR,
                      const pinrec::SlotBeat* cur_node_x_DDR, const pinrec::SlotBeat* cur_node_y_DDR,
                      const pinrec::SlotBeat* cur_probe_x_DDR, const pinrec::SlotBeat* cur_probe_y_DDR,
                      const pinrec::SlotBeat* cur_grad_x_DDR, const pinrec::SlotBeat* cur_grad_y_DDR,
                      pinrec::SlotBeat* next_node_x_DDR, pinrec::SlotBeat* next_node_y_DDR,
                      pinrec::SlotBeat* next_probe_x_DDR, pinrec::SlotBeat* next_probe_y_DDR,
                      pinrec::SlotBeat* inv_precond_DDR,
                      const pinrec::SlotBeat* num_pins_DDR, const pinrec::SlotBeat* area_DDR,
                      const pinrec::SlotBeat* width_DDR, const pinrec::SlotBeat* height_DDR,
                      const UpdateScalars& sc, float ring_BRAM[MAX_BORROWERS][2][pinrec::BANKS][RING_ROWS], float norms_out[4]) {
    int next_row_REG[MAX_BORROWERS][pinrec::BANKS];
#pragma HLS ARRAY_PARTITION variable=next_row_REG complete dim=0
    int beat_count_REG[MAX_BORROWERS];
#pragma HLS ARRAY_PARTITION variable=beat_count_REG complete dim=0
    bool held_REG[MAX_BORROWERS][pinrec::LANES];   // unused by send; reset_exchange's shape
#pragma HLS ARRAY_PARTITION variable=held_REG complete dim=0
    float new_probe_REG[2][pinrec::LANES];         // the beat's new probe positions, for its later groups
#pragma HLS ARRAY_PARTITION variable=new_probe_REG complete dim=0
    SlotReducer a1_sum, a2_sum;
#pragma HLS ARRAY_PARTITION variable=a1_sum.partial complete
#pragma HLS ARRAY_PARTITION variable=a2_sum.partial complete
    a1_sum.clear(); a2_sum.clear();
    int beat_index = 0;

    // Every chunk's owned beats, then the fillers: new preconditioner, the Nesterov step, the die clamp,
    // and each shared slot's new probe position into its borrowers' blocks. Meow.
step_segments:
    for (int seg = 0; seg <= num_chunks; seg++) {
        const pinrec::FusedOwnerDesc own = seg < num_chunks ? owners_DDR[seg] : pinrec::FusedOwnerDesc{};
        const Segment g = segment_of(seg, num_chunks, chunks_DDR, own, filler_beat_offset, num_filler_beats, true);
        reset_exchange(next_row_REG, beat_count_REG, held_REG);
        int t = 0;
        // One op or one private beat per cycle. A beat is stepped at its BOUNDARY op, the first of its span,
        // so that op's group and the span's later ones send the new positions. Meow.
    step_beats:
        for (int i = 0; i < g.num_ops + g.owned_beats - g.shared_beats; i++) {
#pragma HLS LOOP_TRIPCOUNT max=65536
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=next_probe_x_DDR inter false
#pragma HLS DEPENDENCE variable=next_probe_y_DDR inter false
#pragma HLS DEPENDENCE variable=inv_precond_DDR inter false
#pragma HLS DEPENDENCE variable=ring_BRAM inter false
            const FusedOp op = decode_fused_op(i < g.num_ops ? fused_ops_DDR[g.op_offset + i] : pinrec::FUSED_BOUNDARY);
            const int addr = g.base + t;
            pinrec::SlotBeat probe_out[2];
            if (op.boundary) {
                const uint16_t movable = movable_DDR[addr];
                const pinrec::SlotBeat cnx = cur_node_x_DDR[addr], cny = cur_node_y_DDR[addr];
                const pinrec::SlotBeat cpx = cur_probe_x_DDR[addr], cpy = cur_probe_y_DDR[addr];
                const pinrec::SlotBeat cgx = cur_grad_x_DDR[addr], cgy = cur_grad_y_DDR[addr];
                const pinrec::SlotBeat pins = num_pins_DDR[addr], area = area_DDR[addr];
                const pinrec::SlotBeat width = width_DDR[addr], height = height_DDR[addr];
                const pinrec::SlotBeat ipw_old = inv_precond_DDR[addr];
                pinrec::SlotBeat node_x, node_y, ipw_out;
                float a1_terms[pinrec::LANES], a2_terms[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=a1_terms complete
#pragma HLS ARRAY_PARTITION variable=a2_terms complete
                for (int j = 0; j < pinrec::LANES; j++) {
                    const bool m = (movable >> j) & 1;
                    // update_precond_weights. Meow.
                    const float a2 = sc.lambda_area_coef * area.v[j];
                    a1_terms[j] = m ? pins.v[j] : 0.0f;
                    a2_terms[j] = m ? a2 : 0.0f;
                    const float weight = std_max(1.0f, pins.v[j] + a2);
                    const float ipw = sc.precond_enabled ? 1.0f / weight : ipw_old.v[j];
                    // step_all_nodes: u_next = v - step * P^-1 g; v_next = u_next + momentum (u_next - u). Meow.
                    float nx = cpx.v[j] - sc.step_length * (ipw * cgx.v[j]);
                    float ny = cpy.v[j] - sc.step_length * (ipw * cgy.v[j]);
                    float px = nx + sc.momentum_coeff * (nx - cnx.v[j]);
                    float py = ny + sc.momentum_coeff * (ny - cny.v[j]);
                    // enforce_die_boundaries. Meow.
                    float lo_x, hi_x, lo_y, hi_y;
                    die_range(width.v[j], sc.clamp_w, sc.die_w, lo_x, hi_x);
                    die_range(height.v[j], sc.clamp_h, sc.die_h, lo_y, hi_y);
                    nx = std_clamp(nx, lo_x, hi_x); ny = std_clamp(ny, lo_y, hi_y);
                    px = std_clamp(px, lo_x, hi_x); py = std_clamp(py, lo_y, hi_y);
                    node_x.v[j] = m ? nx : cnx.v[j];
                    node_y.v[j] = m ? ny : cny.v[j];
                    probe_out[0].v[j] = m ? px : cpx.v[j];
                    probe_out[1].v[j] = m ? py : cpy.v[j];
                    ipw_out.v[j] = m ? ipw : ipw_old.v[j];
                    new_probe_REG[0][j] = probe_out[0].v[j];
                    new_probe_REG[1][j] = probe_out[1].v[j];
                }
                next_node_x_DDR[addr] = node_x;
                next_node_y_DDR[addr] = node_y;
                inv_precond_DDR[addr] = ipw_out;
                a1_sum.add_beat(beat_index, a1_terms);
                a2_sum.add_beat(beat_index, a2_terms);
                t++;
                beat_index++;
            }
            int probe_addr = addr;
            if (op.beat) {
                probe_addr = own.block_beat_offset[op.borrower] + beat_count_REG[op.borrower];
                send_drain_beat(op, ring_BRAM, beat_count_REG, probe_out);
            } else {
                send_group(op, new_probe_REG, ring_BRAM, next_row_REG);
            }
            // One write site per array per cycle: the owned beat at its boundary, or a block beat. Meow.
            if (op.beat || op.boundary) { next_probe_x_DDR[probe_addr] = probe_out[0]; next_probe_y_DDR[probe_addr] = probe_out[1]; }
        }
    }
    norms_out[0] = a1_sum.total();
    norms_out[1] = a2_sum.total();
    norms_out[2] = 0.0f;
    norms_out[3] = 0.0f;
}

static void iteration_update_CU(
        int                              mode,               // UpdateMode
        int                              num_chunks,
        const pinrec::ChunkDesc*         chunks_DDR,         // [num_chunks]: slot_beat_offset, first_private_slot, first_borrowed_slot
        const pinrec::FusedOwnerDesc*    owners_DDR,         // [num_chunks]: each owner's fused op lists and borrower blocks
        const uint64_t*                  fused_ops_DDR,      // pin_record.hpp FUSED_*
        int                              filler_beat_offset, // the filler tail, in beats
        int                              num_filler_beats,
        const uint16_t*                  movable_DDR,        // per beat: bit j set when slot 16b+j is OWNED or FILLER
        // Per slot, both axes. cur = iteration k (after the host's swap in STEP), next = k + 1. Meow.
        const pinrec::SlotBeat* force_x_DDR, const pinrec::SlotBeat* force_y_DDR,           // COMBINE: electrostatic_force, lambda in
        const pinrec::SlotBeat* cur_node_x_DDR,  const pinrec::SlotBeat* cur_node_y_DDR,
        const pinrec::SlotBeat* cur_probe_x_DDR, const pinrec::SlotBeat* cur_probe_y_DDR,
        const pinrec::SlotBeat* cur_grad_x_DDR,  const pinrec::SlotBeat* cur_grad_y_DDR,
        pinrec::SlotBeat* next_node_x_DDR, pinrec::SlotBeat* next_node_y_DDR,
        pinrec::SlotBeat* next_probe_x_DDR, pinrec::SlotBeat* next_probe_y_DDR,             // STEP writes owned rows and borrowed blocks
        pinrec::SlotBeat* next_grad_x_DDR, pinrec::SlotBeat* next_grad_y_DDR,               // COMBINE: read (owned, blocks), owned rewritten
        pinrec::SlotBeat* inv_precond_DDR,                                                   // 1 / precond_weight: COMBINE reads, STEP writes
        const pinrec::SlotBeat* num_pins_DDR, const pinrec::SlotBeat* area_DDR,             // STEP: constant (fillers' pins 0)
        const pinrec::SlotBeat* width_DDR,    const pinrec::SlotBeat* height_DDR,           // STEP: constant, DBU
        UpdateScalars                    scalars,
        float                            norms_out[4]) {
    // One ring per (borrower, axis, bank): each bank is addressed by at most one entry per cycle. Shared by
    // the two modes, which never run together. Meow.
    ONCHIP_ARRAY float ring_BRAM[MAX_BORROWERS][2][pinrec::BANKS][RING_ROWS];
#pragma HLS ARRAY_PARTITION variable=ring_BRAM complete dim=1
#pragma HLS ARRAY_PARTITION variable=ring_BRAM complete dim=2
#pragma HLS ARRAY_PARTITION variable=ring_BRAM complete dim=3
#pragma HLS BIND_STORAGE variable=ring_BRAM type=ram_2p impl=bram

    if (mode == UPDATE_COMBINE)
        combine_pass(num_chunks, chunks_DDR, owners_DDR, fused_ops_DDR, filler_beat_offset, num_filler_beats, movable_DDR,
                     force_x_DDR, force_y_DDR, cur_probe_x_DDR, cur_probe_y_DDR, cur_grad_x_DDR, cur_grad_y_DDR,
                     next_probe_x_DDR, next_probe_y_DDR, next_grad_x_DDR, next_grad_y_DDR, inv_precond_DDR, ring_BRAM, norms_out);
    else
        step_pass(num_chunks, chunks_DDR, owners_DDR, fused_ops_DDR, filler_beat_offset, num_filler_beats, movable_DDR,
                  cur_node_x_DDR, cur_node_y_DDR, cur_probe_x_DDR, cur_probe_y_DDR, cur_grad_x_DDR, cur_grad_y_DDR,
                  next_node_x_DDR, next_node_y_DDR, next_probe_x_DDR, next_probe_y_DDR, inv_precond_DDR,
                  num_pins_DDR, area_DDR, width_DDR, height_DDR, scalars, ring_BRAM, norms_out);
}

} // namespace plalgo

#endif // PL_ITERATION_UPDATE_CU_HPP
