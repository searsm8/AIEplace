#ifndef PL_ITERATION_UPDATE_SLOT_REDUCE_HPP
#define PL_ITERATION_UPDATE_SLOT_REDUCE_HPP

// slot_reduce -- the device's order for a float sum over slots (#45 step 6): iteration_update_CU's six
// norms. A serial sum cannot take 16 slots per cycle, so: each beat's 16 lane terms by a balanced tree,
// beat b's sum added into partial b % REDUCE_PARTIALS, then a tree over the partials. Plain C++:
// sw_flat's `reductions = "lanes"` includes this file, so the two sum in one order by construction (like
// dhar_tree.hpp). Meow.
//
// The partials are a shift register, not an array indexed by b % 16: each beat adds into the oldest
// (acc[15], which last took beat b - 16) and shifts the result in at acc[0]. Same sums, but the add's
// recurrence is visibly 16 beats long; an indexed array reads to HLS as a distance-1 dependence (II=2,
// csynth 2026-10-29). Meow.

#include "format/pin_record.hpp"

namespace plalgo {

constexpr int REDUCE_PARTIALS = 16;

// ((v0 + v1) + (v2 + v3)) + ... : 16 -> 8 -> 4 -> 2 -> 1, in place. Meow.
static float lane_tree(float v[pinrec::LANES]) {
#pragma HLS INLINE
    for (int width = pinrec::LANES / 2; width > 0; width /= 2) {
#pragma HLS UNROLL
        for (int j = 0; j < width; j++) {
#pragma HLS UNROLL
            v[j] = v[2 * j] + v[2 * j + 1];
        }
    }
    return v[0];
}

struct SlotReducer {
    float acc[REDUCE_PARTIALS];   // acc[k]: the partial that took the beat k beats ago
    int   beats;
    void clear() {
#pragma HLS INLINE
        for (int p = 0; p < REDUCE_PARTIALS; p++) acc[p] = 0.0f;
        beats = 0;
    }
    void add_beat(float terms[pinrec::LANES]) {
#pragma HLS INLINE
        const float sum = acc[REDUCE_PARTIALS - 1] + lane_tree(terms);
        for (int k = REDUCE_PARTIALS - 1; k > 0; k--) acc[k] = acc[k - 1];
        acc[0] = sum;
        beats++;
    }
    // partial[r] (the beats b with b % 16 == r) sits at acc[(beats - 1 - r) % 16]; untouched ones are 0. Meow.
    float total() {
#pragma HLS INLINE
        float partial[REDUCE_PARTIALS];
        for (int r = 0; r < REDUCE_PARTIALS; r++) partial[r] = acc[(beats - 1 - r) & (REDUCE_PARTIALS - 1)];
        for (int width = REDUCE_PARTIALS / 2; width > 0; width /= 2) {
#pragma HLS UNROLL
            for (int j = 0; j < width; j++) {
#pragma HLS UNROLL
                partial[j] = partial[2 * j] + partial[2 * j + 1];
            }
        }
        return partial[0];
    }
};

} // namespace plalgo

#endif // PL_ITERATION_UPDATE_SLOT_REDUCE_HPP
