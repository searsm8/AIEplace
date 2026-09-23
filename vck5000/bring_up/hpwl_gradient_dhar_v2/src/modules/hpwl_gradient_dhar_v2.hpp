#ifndef PL_ALGO_HPWL_GRADIENT_DHAR_V2_HPP
#define PL_ALGO_HPWL_GRADIENT_DHAR_V2_HPP

#ifndef PL_TIER1_STUB
#include <hls_stream.h>
#endif

namespace plalgo {

constexpr int LANES              = 8;
constexpr int MIN_NET_DEGREE     = 2;
constexpr int NET_DEGREE_COUNTS  = LANES - MIN_NET_DEGREE + 1;

struct PinBlock {
    float v[LANES];
};

// Compute the maximum of all degrees in parallel, then return the max of the first `degree` lanes.
static inline float max_scan8(const float v[LANES], int degree) {
        float l1[LANES], l2[LANES], l3[LANES];
#pragma HLS ARRAY_PARTITION variable=l1 complete dim=1
#pragma HLS ARRAY_PARTITION variable=l2 complete dim=1
#pragma HLS ARRAY_PARTITION variable=l3 complete dim=1
        // 1st scan: 2-wide
        // compare each lane with its left neighbor, if any, and propagate the max
        for (int i = 0; i < LANES; i++)
                l1[i] = i < 1 ? v[i] : (v[i] > v[i-1] ? v[i] : v[i-1]);
        // 2nd scan: 4-wide
        // compare each lane with its left neighbor 2 lanes away, if any, and propagate the max
        for (int i = 0; i < LANES; i++)
                l2[i] = i < 2 ? l1[i] : (l1[i] > l1[i-2] ? l1[i] : l1[i-2]);
        // 3rd scan: 8-wide
        // compare each lane with its left neighbor 4 lanes away, if any, and propagate the max
        for (int i = 0; i < LANES; i++)
                l3[i] = i < 4 ? l2[i] : (l2[i] > l2[i-4] ? l2[i] : l2[i-4]);

        return l3[degree-1];
}

static inline float min_scan8(const float v[LANES], int degree) {
    float l1[LANES], l2[LANES], l3[LANES];
#pragma HLS ARRAY_PARTITION variable=l1 complete dim=1
#pragma HLS ARRAY_PARTITION variable=l2 complete dim=1
#pragma HLS ARRAY_PARTITION variable=l3 complete dim=1
    for (int i = 0; i < LANES; i++)
        l1[i] = i < 1 ? v[i] : (v[i] < v[i-1] ? v[i] : v[i-1]);
    for (int i = 0; i < LANES; i++)
        l2[i] = i < 2 ? l1[i] : (l1[i] < l1[i-2] ? l1[i] : l1[i-2]);
    for (int i = 0; i < LANES; i++)
        l3[i] = i < 4 ? l2[i] : (l2[i] < l2[i-4] ? l2[i] : l2[i-4]);

    return l3[degree-1];
}

static void hpwl_gradient_dhar_v2(
        const int*   net_count, // [NET_DEGREE_COUNTS] CUMULATIVE: net_count[i] = nets with degree <= (i+MIN_NET_DEGREE)
        const float* pin_x,     // [num_pins] net-major pin positions (x, or y in a separate call)
        const int*   pin_to_npin,
        const int*   npin_node,
        const float* exp_lut,
        float*       pin_grad,
        float*       node_grad,
        float*       out_hpwl,
        float        inv_gamma,
        float        inv_lut_step,
        int          lut_size,
        int          num_nets,
        int          num_movable,
        int          num_node_pins) {

    int net_count_BRAM[NET_DEGREE_COUNTS]; // cumulative: net_count_BRAM[i] = nets with degree <= (i+MIN_NET_DEGREE)
cache_net_count:
    for (int i = 0; i < NET_DEGREE_COUNTS; i++) {
        net_count_BRAM[i] = net_count[i];
    }

    hls::stream<PinBlock> block_stream;
    hls::stream<int>      degree_stream;

    int pin_offset = 0;

input_controller:
    for (int n = 0; n < num_nets; n++) {
        int degree = MIN_NET_DEGREE;
        for (int k = 0; k < NET_DEGREE_COUNTS; k++) {
            if (n >= net_count_BRAM[k]) degree++;
        }

        PinBlock blk;
        for (int k = 0; k < LANES; k++) {
            blk.v[k] = (k < degree) ? pin_x[pin_offset + k] : pin_x[pin_offset];
        }
        block_stream.write(blk);
        degree_stream.write(degree);

        pin_offset += degree;
    }

compute:
    for (int n = 0; n < num_nets; n++) {
        PinBlock blk = block_stream.read();
        int      d   = degree_stream.read();
        out_hpwl[n] = max_scan8(blk.v, d) - min_scan8(blk.v, d); // Random write, but no data dependency between nets, so II=1
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_GRADIENT_DHAR_V2_HPP
