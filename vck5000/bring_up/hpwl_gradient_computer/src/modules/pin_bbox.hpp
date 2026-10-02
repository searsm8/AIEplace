#ifndef PL_ALGO_PIN_BBOX_HPP
#define PL_ALGO_PIN_BBOX_HPP

// pin_bbox -- stage A of the WA-gradient beat loop (#41): record beat -> pin positions and each
// pin's net bbox. Gathers the 16 pin positions from pos_URAM (bank-major, as hpwl_computer_v2),
// runs the Dhar max/min trees, hands each lane its own net's (max, min) to stage B through a
// stream, and writes the per-net HPWL by-product to DDR.
//
// Large nets (17..96 pins) are hpwl_computer_v2's path: a window of the last MAX_SPAN beats'
// degree-16 max/min, reduced on the net's last real beat. That bbox is known only then, so it goes
// to stage B on its own stream (net_bboxes), once per net; B pops it on the net's first beat and
// stalls until it arrives (_NEW_PLAN_41_large_net_gradient_fifo_20261002.md).
//
// Owns pos_URAM and offset_BRAM inside the beat-loop DATAFLOW region (read only). Meow.

#ifndef PL_TIER1_STUB   // tier-1 harnesses supply an hls::stream stand-in (test/tier1_stub.hpp)
#include <hls_stream.h>
#endif
#include "modules/hpwl_computer_v2.hpp"   // decode_lanes, gather_pin_positions, resolve_degree, reduce_window

namespace plalgo {

// Where a beat sits in the stream: stages B and C decide their per-net stream reads from these,
// so A, which owns the beat counter, is the only stage that resolves them. Meow.
struct BeatFlags {
    bool large;   // a large-net beat (pad or real)
    bool pad;     // packer rule L7: all EMPTY, invisible to net accounting
    bool first;   // first real beat of a large net
    bool last;    // last real beat of a large net
    int  span;    // the large net's real beats (span group)
};

// What one beat leaves stage A with. Per-lane bbox for small nets, so stage B needs no degree/segment
// logic for it; a large net's bbox arrives separately (net_bboxes). Meow.
struct PinBeat {
    float     x[pinrec::LANES];          // pin position (node position + offset)
    float     net_max[pinrec::LANES];    // this lane's net bbox (small nets)
    float     net_min[pinrec::LANES];
    uint32_t  slot_idx[pinrec::LANES];   // for the merge and scatter in stage C
    bool      empty[pinrec::LANES];
    int       degree;                    // LANES on a large-net beat
    BeatFlags flags;
};

struct NetBbox { float hi, lo; };

static void pin_bbox(const pinrec::RecordBeat* records_DDR, int num_beats,
                     const int beat_count_REG[pinrec::NET_DEGREES_PROCESSED],
                     const int span_count_REG[pinrec::SPAN_GROUPS],
                     const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],
                     const float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX],
                     OutBeat* out_beats_DDR, int offset_bits,
                     hls::stream<PinBeat>& pin_beats, hls::stream<NetBbox>& net_bboxes) {
    float hi_window[pinrec::MAX_SPAN] = {}, lo_window[pinrec::MAX_SPAN] = {};   // newest first
#pragma HLS ARRAY_PARTITION variable=hi_window complete dim=0
#pragma HLS ARRAY_PARTITION variable=lo_window complete dim=0
    int beat_in_net = 0;
pin_bbox_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
        const bool large      = beat >= beat_count_REG[pinrec::NET_DEGREES_PROCESSED - 1];
        const int  degree     = large ? pinrec::LANES : resolve_degree(beat, beat_count_REG);
        const int  span       = resolve_span(beat, span_count_REG);
        const int  degree_idx = degree - pinrec::MIN_NET_DEGREE;
        const int  nets_per_beat = pinrec::LANES / degree;
        const DecodedLanes d = decode_lanes(records_DDR[beat], offset_bits);
        const bool pad        = large && d.empty[0];
        const bool first_beat = large && !pad && beat_in_net == 0;
        const bool last_beat  = large && !pad && beat_in_net == span - 1;

        PinBeat pb;
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

        TreeOutputs t = build_tree_outputs(pb.x);
#pragma HLS ARRAY_PARTITION variable=t.max_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=t.min_deg complete dim=0
        for (int i = 0; i < pinrec::LANES; i++) {
            pb.net_max[i]  = t.max_deg[degree_idx][i / degree];
            pb.net_min[i]  = t.min_deg[degree_idx][i / degree];
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
            hi_window[0] = t.max_deg[degree_idx][0];
            lo_window[0] = t.min_deg[degree_idx][0];
        }
        const float net_hi = reduce_window<MaxOp>(hi_window, span);
        const float net_lo = reduce_window<MinOp>(lo_window, span);
        if (large && !pad) beat_in_net = last_beat ? 0 : beat_in_net + 1;

        pin_beats.write(pb);
        if (last_beat) net_bboxes.write(NetBbox{net_hi, net_lo});

        OutBeat out_beat;   // HPWL by-product, as hpwl_computer_v2
#pragma HLS ARRAY_PARTITION variable=out_beat.v complete dim=0
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            const bool live = k < nets_per_beat && !d.empty[k * degree];
            if (large) out_beat.v[k] = k == 0 && last_beat ? net_hi - net_lo : 0.0f;
            else       out_beat.v[k] = live ? t.max_deg[degree_idx][k] - t.min_deg[degree_idx][k] : 0.0f;
        }
        out_beats_DDR[beat] = out_beat;
    }
}

} // namespace plalgo

#endif // PL_ALGO_PIN_BBOX_HPP
