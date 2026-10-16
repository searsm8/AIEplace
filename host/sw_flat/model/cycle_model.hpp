#ifndef SW_FLAT_CYCLE_MODEL_HPP
#define SW_FLAT_CYCLE_MODEL_HPP

// cycle_model.hpp -- device cycles per chunked gradient evaluation (one axis), per phase, from a
// Chunked design's static trip counts and its tail op lists. Started as a copy of
// vck5000/bring_up/beat_packer/cycle_model.hpp @ ef62dba (#47 M0); rewritten for the tail exchange
// (#47 B3, .claude/1_REVIEW/reports/_NEW_PLAN_47_mailbox_redesign_20261006.md):
//
//   TODAY   hpwl_gradient_CU as built (B3): send_tails_pass (owner load + send ops), compute_pass,
//           merge_tails_pass (owned reload + merge ops + owned drain).
//   FUSED   B4: send and merge ride on iteration_update's chunk-major stream, so send_tails_pass and
//           merge_tails_pass's reload / drain disappear. Charged instead: each block's beats, written
//           (send) and read (merge) as if DDR-bound, and merge stalls on a lane that needs more adds
//           than it has adders.
//
// Every loop is II=1; a phase costs its trip count plus loop_overhead per invocation (fitted to two
// RTL co-sims, 2026-10-02). A model, not a measurement of hardware. Density / field-solve time is
// not included. Meow.

#include "beat_packer.hpp"

#include <algorithm>
#include <vector>

namespace packer {

constexpr double CLOCK_HZ        = 300e6;
constexpr long   EXP_LUT_ENTRIES = 1024;    // GRAD_LUT_MAX, an upper bound on the exp LUT the prologue loads. Meow.
inline long      loop_overhead   = 165;

enum class Variant { TODAY, FUSED };

struct Phases {
    long prologue = 0, send_reload = 0, send = 0, load = 0, beats = 0, drain = 0, merge = 0, fold_other = 0;
    long update_send = 0, update_merge = 0;   // added to iteration_update by B4
    long exchange() const { return send + merge + update_send + update_merge; }
    long total() const {
        return prologue + send_reload + send + load + beats + drain + merge + fold_other + update_send + update_merge;
    }
};

inline long ceil_div(long a, long b) { return (a + b - 1) / b; }

// B4 merge: owner j's update streams its slots 16 a beat; a slot gets one add per borrower that holds
// it, and the busiest lane of a beat sets that beat's cycles. Meow.
inline long merge_stall_cycles(const Chunked& ch, int owner, const std::vector<long>& owner_slot, int adders_per_lane) {
    std::vector<int> adds(ch.chunks[owner].enc.num_slots, 0);
    for (const Chunk& c : ch.chunks)
        for (const Chunk::TailBlock& block : c.tail_blocks)
            if (block.owner == owner)
                for (long i = block.first; i < block.first + block.count; i++) adds[owner_slot[c.work_node[c.tail_order[i]]]]++;
    long stalls = 0;
    for (size_t beat = 0; beat * LANES < adds.size(); beat++) {
        int busiest = 0;
        for (int lane = 0; lane < LANES; lane++) busiest = std::max(busiest, adds[beat * LANES + lane]);
        stalls += std::max(0L, ceil_div(busiest, adders_per_lane) - 1);
    }
    return stalls;
}

inline Phases model_cycles(const Chunked& ch, Variant v, int adders_per_lane = 2) {
    Phases p;
    const bool chunked = ch.num_chunks > 1, fused = chunked && v == Variant::FUSED;
    const long O = loop_overhead;
    const std::vector<long> owner_slot = owner_slots_of(ch);
    p.prologue = (long)ch.global.offset_table[0].size() + O + EXP_LUT_ENTRIES + O;
    for (int k = 0; k < ch.num_chunks; k++) {
        const Encoded& e = ch.chunks[k].enc;
        const long slot_beats = e.num_slots / LANES, movable_beats = e.first_fixed_slot / LANES;
        const long owned_beats = e.first_tail_slot / LANES, macro_entries = (long)e.macro_order.size();
        p.load       += slot_beats + O + macro_entries + O;   // load + refresh
        p.beats      += (long)e.issue.size() + NUM_GROUP_ENDS + 2 * O;   // + the dataflow region's fill. Meow.
        p.fold_other += macro_entries + O;                     // the macro-pin fold, compute_pass (C6)
        p.drain      += movable_beats + O;
        if (!chunked) continue;
        if (!fused) {
            p.send_reload += owned_beats + O;
            p.fold_other  += owned_beats + O + owned_beats + O;   // merge_tails_pass: reload + drain owned rows
        } else {
            p.update_merge += merge_stall_cycles(ch, k, owner_slot, adders_per_lane);
        }
    }
    for (const Chunk& c : ch.chunks)   // every block, as its owner exchanges it
        for (const Chunk::TailBlock& block : c.tail_blocks) {
            if (fused) {
                p.update_send  += block.rows * BANKS / LANES;
                p.update_merge += block.rows * BANKS / LANES;
            } else {
                const TailOps ops = block_ops(ch, c, block, owner_slot);
                p.send  += (long)ops.send.size() + O;
                p.merge += (long)ops.merge.size() + O;
            }
        }
    return p;
}

} // namespace packer

#endif // SW_FLAT_CYCLE_MODEL_HPP
