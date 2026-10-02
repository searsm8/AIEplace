#ifndef PL_ALGO_PIN_BBOX_HPP
#define PL_ALGO_PIN_BBOX_HPP

// pin_bbox -- stage A of the WA-gradient beat loop (#41): record beat -> pin positions and each
// pin's net bbox. Gathers the 16 pin positions from pos_URAM (bank-major, as hpwl_computer_v2),
// runs the Dhar max/min trees, hands each lane its own net's (max, min) to stage B through a
// stream, and writes the per-net HPWL by-product to DDR.
//
// Owns pos_URAM and offset_BRAM inside the beat-loop DATAFLOW region (read only). Meow.

#ifndef PL_TIER1_STUB   // tier-1 harnesses supply an hls::stream stand-in (test/tier1_stub.hpp)
#include <hls_stream.h>
#endif
#include "modules/hpwl_computer_v2.hpp"   // decode_lanes, gather_pin_positions, resolve_degree, TreeOutputs

namespace plalgo {

// What one beat leaves stage A with. Per-lane bbox, so stage B needs no degree/segment logic for it. Meow.
struct PinBeat {
    float    x[pinrec::LANES];          // pin position (node position + offset)
    float    net_max[pinrec::LANES];    // this lane's net bbox
    float    net_min[pinrec::LANES];
    uint32_t slot_idx[pinrec::LANES];   // for the merge and scatter in stage C
    bool     empty[pinrec::LANES];
    int      degree;
};

static void pin_bbox(const pinrec::RecordBeat* records_DDR, int num_beats,
                     const int beat_count_REG[pinrec::NET_DEGREES_PROCESSED],
                     const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],
                     const float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX],
                     OutBeat* out_beats_DDR, int offset_bits,
                     hls::stream<PinBeat>& pin_beats) {
pin_bbox_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
        const int degree        = resolve_degree(beat, beat_count_REG);
        const int degree_idx    = degree - pinrec::MIN_NET_DEGREE;
        const int nets_per_beat = pinrec::LANES / degree;
        const DecodedLanes d = decode_lanes(records_DDR[beat], offset_bits);

        PinBeat pb;
#pragma HLS ARRAY_PARTITION variable=pb.x complete dim=0
#pragma HLS ARRAY_PARTITION variable=pb.net_max complete dim=0
#pragma HLS ARRAY_PARTITION variable=pb.net_min complete dim=0
#pragma HLS ARRAY_PARTITION variable=pb.slot_idx complete dim=0
#pragma HLS ARRAY_PARTITION variable=pb.empty complete dim=0
        gather_pin_positions(d, pos_URAM, offset_BRAM, pb.x);

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
        pin_beats.write(pb);

        OutBeat out_beat;   // HPWL by-product, as hpwl_computer_v2
#pragma HLS ARRAY_PARTITION variable=out_beat.v complete dim=0
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            const bool live = k < nets_per_beat && !d.empty[k * degree];
            out_beat.v[k] = live ? t.max_deg[degree_idx][k] - t.min_deg[degree_idx][k] : 0.0f;
        }
        out_beats_DDR[beat] = out_beat;
    }
}

} // namespace plalgo

#endif // PL_ALGO_PIN_BBOX_HPP
