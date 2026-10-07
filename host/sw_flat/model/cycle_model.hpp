#ifndef SW_FLAT_CYCLE_MODEL_HPP
#define SW_FLAT_CYCLE_MODEL_HPP

// cycle_model.hpp -- device cycles per chunked gradient evaluation (one axis), per phase, from a
// Chunked design's static trip counts. Copied from vck5000/bring_up/beat_packer/cycle_model.hpp @
// ef62dba (#47 M0), its widened-mailbox branch dropped, and extended with the #47 redesign variants
// (.claude/1_REVIEW/reports/_NEW_PLAN_47_mailbox_redesign_20261006.md):
//
//   TODAY        hpwl_gradient_CU as built: send pass, compute pass, fold pass.
//   FUSED        R1 + R4: iteration_update emits shared positions (no send pass) and merge-adds the
//                returned gradients (no fold pass); receive and return kept.
//   FUSED_TAIL   + R2(b)/R3(b): externals in tail rows, so load brings them in and drain sends them
//                out; receive and return gone. The write-side lane permutation is charged NOTHING
//                here -- M1 measures whether it is needed.
//
// The update pass exists in every variant, so only its ADDED work is charged: the extra parcel
// beats it writes / reads (as if DDR-bound, the conservative case) and merge stalls on a lane that
// needs more adds than it has adders. Every loop is II=1; a phase costs its trip count plus
// loop_overhead per invocation. Calibration (two RTL co-sims, 2026-10-02): loop_overhead 165,
// non-burst send/collect 4.26 cycles per entry; the burst fix (#45 2c) makes that 1.
// A model, not a measurement of hardware. Density / field-solve time is not included. Meow.

#include "beat_packer.hpp"

#include <algorithm>
#include <vector>

namespace packer {

constexpr double CLOCK_HZ        = 300e6;
constexpr long   EXP_LUT_ENTRIES = 1024;    // GRAD_LUT_MAX, an upper bound on the exp LUT the prologue loads. Meow.
inline long      loop_overhead   = 165;
inline double    unburst_cycles  = 1.0;     // per send / collect entry; 1 since the #45 2c burst fix. Meow.

enum class Variant { TODAY, FUSED, FUSED_TAIL };

struct Phases {
    long prologue = 0, send_reload = 0, send = 0, load = 0, receive = 0, beats = 0, ret = 0, drain = 0,
         collect = 0, fold_other = 0;
    // Added to iteration_update by the redesign; macro_fold is the macro-pin fold moved out of the
    // fold pass (placement undecided, plan decision 2), charged at its fold-pass cost. Meow.
    long update_send = 0, update_merge = 0, macro_fold = 0;
    long mailbox() const { return send + receive + ret + collect; }
    long total() const {
        return prologue + send_reload + send + load + receive + beats + ret + drain + collect + fold_other +
               update_send + update_merge + macro_fold;
    }
};

// Facts about one chunk's layout that the variants need beyond the trip counts. Meow.
struct ChunkFacts {
    long tail_extra_slot_beats = 0;   // FUSED_TAIL: slot beats added by starting the tail at a common row
    long merge_stall_cycles    = 0;   // R4: beats whose busiest lane needs more adds than adders_per_lane
    long shared_entries        = 0;   // real (non-padding) owner-side entries
    long shared_padding        = 0;   // padding entries in shared_local (today's model charges them)
    long shared_macro_pins     = 0;   // shared entries that are MACRO_PIN slots (R1 must refresh them)
};

inline long ceil_div(long a, long b) { return (a + b - 1) / b; }

// Tail rows: today bank b holds own_b + ext_b movable rows, interleaved, so movable rows =
// max_b(own_b + ext_b). With the externals in rows after every bank's owned rows, starting at one
// common row, movable rows = max_b(own_b) + max_b(ext_b). Same colouring (M1 may change it). Meow.
inline ChunkFacts chunk_facts(const Chunk& c, int adders_per_lane) {
    ChunkFacts f;
    const Encoded& e = c.enc;
    std::vector<long> own_rows(BANKS, 0), ext_rows(BANKS, 0);
    for (size_t local = 0; local < e.kind.size(); local++) {
        if (e.bank[local] == NO_BANK || slot_class(e.kind[local]) == 0) continue;
        (c.external[local] ? ext_rows : own_rows)[e.bank[local]]++;
    }
    long interleaved = 0;
    for (int b = 0; b < BANKS; b++) interleaved = std::max(interleaved, own_rows[b] + ext_rows[b]);
    const long tail_rows = *std::max_element(own_rows.begin(), own_rows.end()) +
                           *std::max_element(ext_rows.begin(), ext_rows.end());
    f.tail_extra_slot_beats = (tail_rows - interleaved) * BANKS / LANES;

    // R4: a shared slot receives one add per consumer that holds it. Per 16-slot update beat, the
    // busiest lane sets the cycles. Meow.
    std::vector<int> adds_per_slot(e.num_slots, 0);
    for (int local : c.shared_local) {
        if (local < 0) { f.shared_padding++; continue; }
        f.shared_entries++;
        if (e.kind[local] == MACRO_PIN) f.shared_macro_pins++;
        adds_per_slot[e.node_slot[local]]++;
    }
    for (long beat = 0; beat < e.num_slots / LANES; beat++) {
        int busiest = 0;
        for (int lane = 0; lane < LANES; lane++) busiest = std::max(busiest, adds_per_slot[beat * LANES + lane]);
        f.merge_stall_cycles += std::max(0L, ceil_div(busiest, adders_per_lane) - 1);
    }
    return f;
}

inline Phases model_cycles(const Chunked& ch, Variant v, int adders_per_lane = 1) {
    Phases p;
    const bool chunked = ch.num_chunks > 1;
    const bool fused = chunked && v != Variant::TODAY, tail = chunked && v == Variant::FUSED_TAIL;
    const long O = loop_overhead;
    p.prologue = (long)ch.global.offset_table[0].size() + O + EXP_LUT_ENTRIES + O;
    for (int k = 0; k < ch.num_chunks; k++) {
        const Chunk& c = ch.chunks[k];
        const Encoded& e = c.enc;
        const ChunkFacts f = chunk_facts(c, adders_per_lane);
        const long extra = tail ? f.tail_extra_slot_beats : 0;
        const long slot_beats = e.num_slots / LANES + extra, movable_beats = e.first_fixed_slot / LANES + extra;
        const long macro_entries = (long)e.macro_order.size();
        const long owner_trips = (long)c.shared_local.size(), owner_loops = (long)c.parcels.size();
        const long consumer_trips = (long)c.external_local.size();
        long parcel_beats = 0;   // the update's extra stream beats: each real parcel, 16 entries a beat
        for (const Parcel& parcel : c.parcels) if (parcel.offset >= 0) parcel_beats += ceil_div(parcel.count, LANES);

        p.load  += slot_beats + O + macro_entries + O;
        p.beats += (long)e.issue.size() + NUM_GROUP_ENDS + 2 * O;   // + the dataflow region's fill. Meow.
        p.drain += movable_beats + O;
        if (!chunked) { p.fold_other += macro_entries + O; continue; }
        if (!tail) { p.receive += consumer_trips + O; p.ret += consumer_trips + O; }
        if (fused) {
            p.update_send  += parcel_beats;
            p.update_merge += parcel_beats + f.merge_stall_cycles;
            p.macro_fold   += macro_entries + O;
        } else {
            p.send_reload += slot_beats + O + macro_entries + O;
            p.send        += (long)(owner_trips * unburst_cycles) + owner_loops * O;
            p.collect     += (long)(owner_trips * unburst_cycles) + owner_loops * O;
            p.fold_other  += movable_beats + O + macro_entries + O + movable_beats + O;
        }
    }
    return p;
}

} // namespace packer

#endif // SW_FLAT_CYCLE_MODEL_HPP
