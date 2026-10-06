#ifndef CYCLE_MODEL_HPP
#define CYCLE_MODEL_HPP

// cycle_model.hpp -- device cycles per gradient evaluation (one axis) of hpwl_gradient_computer_v2,
// per phase, from a Chunked design's descriptor trip counts (#42). Every loop is II=1, so a phase
// costs its trip count plus loop_overhead per loop invocation (pipeline fill + m_axi latency).
// EXCEPT the 1-float send write and collect read of the mailbox: neither is inferred as a burst
// (send's write sits behind `if (slot < 0) continue`), so each waits on DDR latency with 16
// requests in flight. Fitted to two RTL co-sims of v2 (2026-10-02; K=10 / 953 external -> 62,573
// cycles, K=2 / 2,275 external -> 37,373): loop_overhead 165 cycles, send/collect 4.26 cycles per
// entry (= the co-sim AXI model's 64-cycle latency / 16 outstanding). Real DDR latency is likely
// higher, so 4.26 is a floor. On the real designs invocations are < 1% of the cycles.
// With a WideMailbox, the four mailbox loops move a 16-entry beat per cycle (bring_up/mailbox_widened).
// A model, not a measurement of hardware. Density / field-solve time is not included. Meow.

#include "beat_packer.hpp"
#include "mailbox_layout.hpp"

namespace packer {

constexpr double CLOCK_HZ        = 300e6;   // Mark, 2026-10-02. v2 does NOT yet meet 3.33 ns (negative slack). Meow.
constexpr int    ITERATIONS      = 1000;
constexpr int    GRADIENT_AXES   = 2;
constexpr long   EXP_LUT_ENTRIES = 1024;    // GRAD_LUT_MAX, an upper bound on the exp LUT the prologue loads. Meow.
inline long      loop_overhead   = 165;     // cycles per loop invocation (see above). Meow.
inline double    unburst_cycles  = 4.26;    // per 1-float send / collect entry (see above). Meow.

struct Phases {
    long prologue = 0, send_reload = 0, send = 0, load = 0, receive = 0, beats = 0, ret = 0, drain = 0, collect = 0, fold_other = 0;
    long mailbox() const { return send + receive + ret + collect; }
    long total() const { return prologue + send_reload + send + load + receive + beats + ret + drain + collect + fold_other; }
};

inline Phases model_cycles(const Chunked& ch, const WideMailbox* wide) {
    Phases p;
    const bool chunked = ch.num_chunks > 1;
    const long O = loop_overhead;
    p.prologue = (long)ch.global.offset_table[0].size() + O + EXP_LUT_ENTRIES + O;
    for (int k = 0; k < ch.num_chunks; k++) {
        const Encoded& e = ch.chunks[k].enc;
        const long slot_beats = e.num_slots / LANES, movable_beats = e.first_fixed_slot / LANES;
        const long macro_entries = (long)e.macro_order.size();
        // Per-cycle trip counts of the owner-side (send, collect) and consumer-side (receive, return)
        // mailbox loops, and the owner side's inner-loop invocations. Meow.
        long owner_trips = (long)ch.chunks[k].shared_local.size(), owner_loops = (long)ch.chunks[k].parcels.size();
        long consumer_trips = (long)ch.chunks[k].external_local.size();
        if (wide) {
            owner_trips = (long)wide->shared_lanes[k].size() / LANES;
            owner_loops = (long)wide->shared_parcels[k].size();
            consumer_trips = (long)wide->external_lanes[k].size() / LANES;
        }
        if (chunked) {
            p.send_reload += slot_beats + O + macro_entries + O;
            p.send        += (long)(owner_trips * (wide ? 1.0 : unburst_cycles)) + owner_loops * O;
            p.receive     += consumer_trips + O;
        }
        p.load  += slot_beats + O + macro_entries + O;
        p.beats += (long)e.issue.size() + GROUP_COUNTS + 2 * O;   // + the dataflow region's fill. Meow.
        if (chunked) p.ret += consumer_trips + O;
        p.drain += movable_beats + O;
        if (chunked) {
            p.collect    += (long)(owner_trips * (wide ? 1.0 : unburst_cycles)) + owner_loops * O;
            p.fold_other += movable_beats + O + macro_entries + O + movable_beats + O;
        } else {
            p.fold_other += macro_entries + O;
        }
    }
    return p;
}

inline double device_seconds(const Phases& p) { return (double)ITERATIONS * GRADIENT_AXES * p.total() / CLOCK_HZ; }

} // namespace packer

#endif // CYCLE_MODEL_HPP
