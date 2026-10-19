#ifndef SW_FLAT_CYCLE_MODEL_HPP
#define SW_FLAT_CYCLE_MODEL_HPP

// cycle_model.hpp -- device cycles per chunked gradient evaluation (one axis), per phase, from a
// Chunked design's static trip counts and its tail op lists. Started as a copy of
// vck5000/bring_up/beat_packer/cycle_model.hpp @ ef62dba (#47 M0); rewritten for the tail exchange
// (#47 B3, .claude/1_REVIEW/reports/_NEW_PLAN_47_mailbox_redesign_20261006.md):
//
//   TODAY   hpwl_gradient_CU as built (B3 + C8): send_tails_pass (shared-row load + send ops),
//           compute_pass, merge_tails_pass (shared-row reload + merge ops + shared-row drain).
//   FUSED   B4: send and merge ride on iteration_update's chunk-major stream, so send_tails_pass and
//           merge_tails_pass's reload / drain disappear. Charged instead: each block's beats, written
//           (send) and read (merge) as if DDR-bound, and merge stalls on a lane that needs more adds
//           than it has adders.
//   RESIDENT  R: every shared node has one slot, the same in every chunk, in a region that stays in
//           URAM for the whole call. Chunk rows: private [0, P_k) | gap to P_max | region
//           [P_max, P_max + R) | fixed. The region must start at the same row in every chunk to
//           persist, hence P_max. Region loaded once, drained once; no tails, no send, no merge.
//           Macro pins stay per-chunk local slots (C6): refreshed and folded in every chunk.
//   RESIDENT_MACROS  R+M: R with every macro and macro pin in the region too, so refresh runs once
//           after the region load and the fold once before the region drain, not per chunk.
//   Both re-encode each chunk with its region nodes held to one global colouring (shared_bank).
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
    long region = 0;                          // R: the region's load and drain (+ R+M: its refresh and fold)
    long exchange() const { return send + merge + update_send + update_merge; }
    long total() const {
        return prologue + send_reload + send + load + beats + drain + merge + fold_other + update_send + update_merge + region;
    }
};

// The resident layout of a Chunked design: which nodes the region holds, its rows under one global
// colouring, and each chunk re-encoded around those banks. Meow.
struct Resident {
    long region_nodes = 0, region_rows = 0, max_private_rows = 0;
    long shared_cells = 0, shared_macros = 0, other_macros = 0, macro_pins = 0;   // region_nodes by kind
    long region_macro_entries = 0;               // R+M: the global refresh / fold list, hazard padding included
    std::vector<long> private_rows, fixed_rows, issue_beats, macro_entries;   // per chunk
    long max_slots = 0;
};

inline Resident build_resident(const Chunked& ch, const Config& cfg, bool all_macros) {
    const Encoded& g = ch.global;
    const int num_global = (int)g.kind.size();
    std::vector<char> in_region(num_global, 0), shared(num_global, 0);
    for (const Chunk& c : ch.chunks)
        for (size_t l = 0; l < c.work_node.size(); l++) if (c.external[l]) in_region[c.work_node[l]] = shared[c.work_node[l]] = 1;
    if (all_macros)
        for (int n = 0; n < num_global; n++) if (g.kind[n] == MACRO || g.kind[n] == MACRO_PIN) in_region[n] = 1;

    Encoded colour = g;
    assign_banks(colour, cfg);
    Resident r;
    std::vector<long> region_per_bank(BANKS, 0);
    for (int n = 0; n < num_global; n++) {
        if (!in_region[n]) continue;
        region_per_bank[colour.bank[n]]++;
        r.region_nodes++;
        if (g.kind[n] == MACRO_PIN) r.macro_pins++;
        else if (g.kind[n] == MACRO) (shared[n] ? r.shared_macros : r.other_macros)++;
        else r.shared_cells++;
    }
    r.region_rows = *std::max_element(region_per_bank.begin(), region_per_bank.end());
    if (all_macros) { schedule_macro_pins(colour, cfg); r.region_macro_entries = (long)colour.macro_order.size(); }

    for (const Chunk& c : ch.chunks) {
        const Encoded& src = c.enc;
        Encoded e;
        e.work = src.work; e.kind = src.kind; e.base_node = src.base_node; e.pinned_offset = src.pinned_offset;
        e.unique_nodes = src.unique_nodes; e.in_scope_nets = src.in_scope_nets;
        if (!all_macros) e.macro_pins = src.macro_pins;   // R+M: no per-chunk refresh / fold list
        for (int axis = 0; axis < 2; axis++) e.offset_table[axis] = src.offset_table[axis];
        e.shared_bank.assign(c.work_node.size(), NO_BANK);
        for (size_t l = 0; l < c.work_node.size(); l++)
            if (in_region[c.work_node[l]]) e.shared_bank[l] = colour.bank[c.work_node[l]];
        encode_resolved(e, cfg);
        std::vector<long> private_per_bank(BANKS, 0), fixed_per_bank(BANKS, 0);
        for (size_t l = 0; l < c.work_node.size(); l++) {
            if (e.bank[l] == NO_BANK || in_region[c.work_node[l]]) continue;
            (slot_class(e.kind[l]) == 0 ? fixed_per_bank : private_per_bank)[e.bank[l]]++;
        }
        r.private_rows.push_back(*std::max_element(private_per_bank.begin(), private_per_bank.end()));
        r.fixed_rows.push_back(*std::max_element(fixed_per_bank.begin(), fixed_per_bank.end()));
        r.issue_beats.push_back((long)e.issue.size());
        r.macro_entries.push_back((long)e.macro_order.size());
    }
    r.max_private_rows = *std::max_element(r.private_rows.begin(), r.private_rows.end());
    for (long fixed : r.fixed_rows) r.max_slots = std::max(r.max_slots, (r.max_private_rows + r.region_rows + fixed) * BANKS);
    return r;
}

inline Phases model_resident(const Chunked& ch, const Resident& r, bool all_macros) {
    Phases p;
    const long O = loop_overhead, beats_per_row = BANKS / LANES;
    p.prologue = (long)ch.global.offset_table[0].size() + O + EXP_LUT_ENTRIES + O;
    p.region   = 2 * (r.region_rows * beats_per_row + O);   // load (zeroing grad alongside) + drain
    if (all_macros) p.region += 2 * (r.region_macro_entries + O);   // refresh once, fold once
    for (int k = 0; k < ch.num_chunks; k++) {
        p.load  += (r.private_rows[k] + r.fixed_rows[k]) * beats_per_row + 2 * O;   // two ranges around the region
        p.beats += r.issue_beats[k] + NUM_GROUP_ENDS + 2 * O;
        p.drain += r.private_rows[k] * beats_per_row + O;
        if (!all_macros) {
            p.load       += r.macro_entries[k] + O;   // refresh
            p.fold_other += r.macro_entries[k] + O;
        }
    }
    return p;
}

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
        const long shared_beats = e.first_private_slot / LANES, macro_entries = (long)e.macro_order.size();
        p.load       += slot_beats + O + macro_entries + O;   // load + refresh
        p.beats      += (long)e.issue.size() + NUM_GROUP_ENDS + 2 * O;   // + the dataflow region's fill. Meow.
        p.fold_other += macro_entries + O;                     // the macro-pin fold, compute_pass (C6)
        p.drain      += movable_beats + O;
        if (!chunked) continue;
        if (!fused) {
            p.send_reload += shared_beats + O;
            p.fold_other  += shared_beats + O + shared_beats + O;   // merge_tails_pass: reload + drain shared rows (C8)
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
