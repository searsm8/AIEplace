#ifndef PL_HPWL_GRADIENT_HPWL_CU_HPP
#define PL_HPWL_GRADIENT_HPWL_CU_HPP

// hpwl_CU -- stage A of the WA-gradient beat loop (#41): record beat -> pin positions and each
// pin's net bbox. Gathers the 16 pin positions from pos_URAM (bank-major, as hpwl_computer_v2),
// runs the Dhar max/min trees, hands each lane its own net's (max, min) to stage B through a
// stream, and sums every net's HPWL into hpwl_total_fixed (pinrec::hpwl_to_fixed units).
//
// Large nets (17..96 pins) are hpwl_computer_v2's path: a window of the last MAX_SPAN beats'
// degree-16 max/min, reduced on the net's last real beat. That bbox is known only then, so it goes
// to stage B on its own stream (net_bboxes), once per net; B pops it on the net's first beat and
// stalls until it arrives (PLAN_41_large_net_gradient_fifo_20261002.md).
//
// Owns pos_URAM and offset_BRAM inside the beat-loop DATAFLOW region (read only). Meow.
// From vck5000/bring_up/hpwl_gradient_computer/src/modules/pin_bbox.hpp @ 41f2193 (#45). Meow.

#ifndef PL_TIER1_STUB   // tier-1 harnesses supply an hls::stream stand-in (test/tier1_stub.hpp)
#include <hls_stream.h>
#endif
#include "modules/hpwl_gradient/record_gather.hpp"   // decode_lanes, gather_pin_positions, resolve_group, balanced_tree
#include "modules/hpwl_gradient/dhar_tree.hpp"       // dhar_tree, MaxOp/MinOp

namespace plalgo {

// Where a beat sits in the stream: stages B and C decide their per-net stream reads from these,
// so A, which owns the beat counter, is the only stage that resolves them. Meow.
struct BeatFlags {
    bool large;   // a large-net beat (pad or real)
    bool pad;     // packer rule L7: all EMPTY, invisible to net accounting
    bool first;   // first real beat of a large net
    bool last;    // last real beat of a large net
    int  span;    // beats the net occupies: 1 for a small net, 2..MAX_SPAN for a large one. Meow.
};

// What one beat leaves stage A with. One bbox per small net in the beat, not per lane: stage B maps
// lane i to net i / degree, which halves the bbox width of the A->B FIFO. A large net's bbox arrives
// separately (net_bboxes). Meow.
struct PinBeat {
    float     x[pinrec::LANES];                // pin position (node position + offset)
    float     net_max[pinrec::MAX_NETS_PER_BEAT];   // bbox of the beat's k-th small net
    float     net_min[pinrec::MAX_NETS_PER_BEAT];
    uint32_t  slot_idx[pinrec::LANES];   // for the merge and scatter in stage C
    bool      empty[pinrec::LANES];
    int       degree;                    // LANES on a large-net beat
    BeatFlags flags;
};

struct NetBbox { float hi, lo; };

static void hpwl_CU(const pinrec::RecordBeat* records_DDR, int num_beats,
                    const int group_end_REG[pinrec::NUM_GROUP_ENDS],
                    const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],
                    const float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX],
                    int offset_bits, hls::stream<PinBeat>& pin_beats, hls::stream<NetBbox>& net_bboxes,
                    int64_t& hpwl_total_fixed) {
    float hi_window[pinrec::MAX_SPAN] = {}, lo_window[pinrec::MAX_SPAN] = {};   // newest first
#pragma HLS ARRAY_PARTITION variable=hi_window complete dim=0
#pragma HLS ARRAY_PARTITION variable=lo_window complete dim=0
    int beat_in_net = 0;
    int64_t hpwl_sum = 0;   // integer add: one cycle, so no partial sums needed for II=1. Meow.
hpwl_CU_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
        const int  group      = resolve_group(beat, group_end_REG);
        const bool large      = group >= pinrec::NET_DEGREES_PROCESSED;
        const int  degree     = large ? pinrec::LANES : group + pinrec::MIN_NET_DEGREE;
        const int  span       = large ? group - pinrec::NET_DEGREES_PROCESSED + 2 : 1;
        const int  degree_idx = degree - pinrec::MIN_NET_DEGREE;
        const int  nets_per_beat = pinrec::LANES / degree;
        const DecodedLanes d = decode_lanes(records_DDR[beat], offset_bits);
        const bool pad        = large && d.empty[0];
        const bool first_beat = large && !pad && beat_in_net == 0;
        const bool last_beat  = large && !pad && beat_in_net == span - 1;

        PinBeat pb;
        // restrict PinBeat to registers, not memory.
#pragma HLS ARRAY_PARTITION variable=pb.x complete dim=0
#pragma HLS ARRAY_PARTITION variable=pb.net_max complete dim=0
#pragma HLS ARRAY_PARTITION variable=pb.net_min complete dim=0
#pragma HLS ARRAY_PARTITION variable=pb.slot_idx complete dim=0
#pragma HLS ARRAY_PARTITION variable=pb.empty complete dim=0
        gather_pin_positions(d, pos_URAM, offset_BRAM, pb.x);

        // A large-net beat's EMPTY lanes trail its pins; lane 0's pin stands in, neutral to max and
        // min (stage B gives EMPTY lanes zero weight, so the stand-in never reaches a sum). Meow.
        for (int i = 1; i < pinrec::LANES; i++)
            if (large && d.empty[i]) pb.x[i] = pb.x[0];

        // Every degree's per-net bbox at once (Dhar trees); degree_idx picks this beat's row. Meow.
        float net_max[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
        float net_min[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=net_max complete dim=0
#pragma HLS ARRAY_PARTITION variable=net_min complete dim=0
        dhar_tree<MaxOp>(pb.x, net_max);
        dhar_tree<MinOp>(pb.x, net_min);
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            pb.net_max[k] = net_max[degree_idx][k];
            pb.net_min[k] = net_min[degree_idx][k];
        }
        for (int i = 0; i < pinrec::LANES; i++) {
            pb.slot_idx[i] = d.slot_idx[i];
            pb.empty[i]    = d.empty[i];
        }
        pb.degree = degree;
        pb.flags  = BeatFlags{large, pad, first_beat, last_beat, span};

        if (!pad) {
            for (int w = pinrec::MAX_SPAN - 1; w > 0; w--) {
                hi_window[w] = hi_window[w - 1];
                lo_window[w] = lo_window[w - 1];
            }
            hi_window[0] = net_max[degree_idx][0];
            lo_window[0] = net_min[degree_idx][0];
        }
        const float net_hi = balanced_tree<MaxOp>(hi_window, span);
        const float net_lo = balanced_tree<MinOp>(lo_window, span);
        if (large && !pad) beat_in_net = last_beat ? 0 : beat_in_net + 1;

        pin_beats.write(pb);
        if (last_beat) net_bboxes.write(NetBbox{net_hi, net_lo});

        int64_t beat_hpwl = 0;   // up to 8 nets: float->int64 per net, then an adder tree. Meow.
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            const bool live = k < nets_per_beat && !d.empty[k * degree];
            float net_hpwl;
            if (large) net_hpwl = k == 0 && last_beat ? net_hi - net_lo : 0.0f;
            else       net_hpwl = live ? net_max[degree_idx][k] - net_min[degree_idx][k] : 0.0f;
            beat_hpwl += pinrec::hpwl_to_fixed(net_hpwl);
        }
        hpwl_sum += beat_hpwl;
    }
    hpwl_total_fixed = hpwl_sum;
}

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_HPWL_CU_HPP
