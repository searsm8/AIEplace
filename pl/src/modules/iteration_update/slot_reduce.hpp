#ifndef PL_ITERATION_UPDATE_SLOT_REDUCE_HPP
#define PL_ITERATION_UPDATE_SLOT_REDUCE_HPP

// slot_reduce -- the device's order for a float sum over slots (#45 step 6): iteration_update_CU's six
// norms. A serial sum cannot take 16 slots per cycle, so: each beat's 16 lane terms by a balanced tree,
// beat sums added into REDUCE_PARTIALS rotating partials (partial[beat % REDUCE_PARTIALS], so a partial's
// read-add-write comes round every REDUCE_PARTIALS beats, longer than any fadd pipeline), then a tree over
// the partials. Plain C++: sw_flat's `reductions = "lanes"` includes this file, so the two sum in one
// order by construction (like dhar_tree.hpp). Meow.

#include "format/pin_record.hpp"

namespace plalgo {

constexpr int REDUCE_PARTIALS = 16;

// ((v0 + v1) + (v2 + v3)) + ... : 16 -> 8 -> 4 -> 2 -> 1, in place. Meow.
static float lane_tree(float v[pinrec::LANES]) {
#pragma HLS INLINE
    for (int width = pinrec::LANES / 2; width > 0; width /= 2)
        for (int j = 0; j < width; j++) v[j] = v[2 * j] + v[2 * j + 1];
    return v[0];
}

struct SlotReducer {
    float partial[REDUCE_PARTIALS];
    void clear() {
#pragma HLS INLINE
        for (int p = 0; p < REDUCE_PARTIALS; p++) partial[p] = 0.0f;
    }
    void add_beat(int beat, float terms[pinrec::LANES]) {
#pragma HLS INLINE
        const float beat_sum = lane_tree(terms);
        partial[beat % REDUCE_PARTIALS] += beat_sum;
    }
    float total() {
#pragma HLS INLINE
        for (int width = REDUCE_PARTIALS / 2; width > 0; width /= 2)
            for (int j = 0; j < width; j++) partial[j] = partial[2 * j] + partial[2 * j + 1];
        return partial[0];
    }
};

} // namespace plalgo

#endif // PL_ITERATION_UPDATE_SLOT_REDUCE_HPP
