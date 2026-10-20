#ifndef DENSITY_FOOTPRINT_HPP
#define DENSITY_FOOTPRINT_HPP

// density_footprint.hpp -- the host->device contract for bin_density_CU. Shared by the host packer and
// the device, so neither side can drift. Plain C++ with fixed-width types: g++ and Vitis HLS alike.
//
// Per slot, the device reads its position (the gradient's slot-major pos arrays, lower-left corner,
// die frame) and a footprint index: 16 per 512-bit beat, aligned beat-for-beat with the positions.
// The index selects an entry of a small on-chip table of distinct footprints, the way a pin record's
// offset_idx selects a pin offset (pin_record.hpp): cells come from a library of few sizes, and every
// filler shares one entry. Slots that deposit nothing (tail copies, pin slots, EMPTY padding)
// carry NO_DEPOSIT. Fixed nodes are not in the stream: the host deposits them once into a base map,
// already capped at min(rho, td) (#35), which the device loads as the accumulator's starting value. Meow.

#include "format/pin_record.hpp"

#include <cmath>
#include <cstdint>

namespace densrec {

#ifndef PL_DENSITY_GRID_MAX
#define PL_DENSITY_GRID_MAX 1024   // override (-D) only to shrink RTL co-simulation
#endif
constexpr int      GRID_MAX            = PL_DENSITY_GRID_MAX;   // v1 hardware grid; 2048 is the planned extension
constexpr int      FOOTPRINT_TABLE_MAX = 4096;          // assumed, not yet measured on the 44 designs
constexpr uint32_t NO_DEPOSIT          = 0xFFFFFFFFu;
constexpr int      URAM_WORD_WIDTH     = 36;            // a URAM word in 8K x 36 mode: one bin's fixed-point accumulator
constexpr int      WINDOW_BINS         = 4;             // bins per axis a fast-path footprint may span (4x4 banks)

struct FootprintBeat { uint32_t idx[pinrec::LANES]; };  // one 512-bit DDR beat of footprint indices
struct FixedBeat     { uint32_t q[pinrec::LANES]; };    // one 512-bit DDR beat of a fixed-point map (the base)
struct SlowSlot      { uint32_t slot, footprint; };     // a slot whose footprint may span > WINDOW_BINS bins

// The fraction bits F for one design (Mark, 2026-10-20: the total-area bound). Every bin holds part of
// the total deposit, so no bin can exceed it: integer bits = floor(log2 total) + 2 is ceil plus one
// guard bit, which absorbs the <= 1/2-unit round-up of each deposit. Capped at 31 so one deposit
// (<= one bin area) still fits to_fixed's uint32. Host-side only.
// REPORT_48_bin_density_fixed_point_20261019.md. Meow.
inline int rho_frac_bits(double total_bin_areas) {
    int integer_bits = 1;
    while (integer_bits < URAM_WORD_WIDTH && std::ldexp(1.0, integer_bits - 1) <= total_bin_areas) integer_bits++;
    const int frac_bits = URAM_WORD_WIDTH - integer_bits;
    return frac_bits < 31 ? frac_bits : 31;
}

// A count of fixed units (>= 0, < 2^32) to the nearest integer: the float add rounds, the conversion
// truncates. The ONE rounding the host and bin_density_CU both use, so their maps agree bit for bit. Meow.
inline uint32_t to_fixed(float units) { return (uint32_t)(units + 0.5f); }

// One distinct footprint: half the real size and half the clamped size, kept separate so that
// xl = x + half_w - half_cw is the golden's float expression (sw_flat density.cpp node_footprint).
struct Footprint { float half_w, half_h, half_cw, half_ch, weight; };

// computeNodeFootprint (sw_flat density.cpp:18): the sqrt(2)-bin clamp with an area-conserving weight,
// and a movable macro below td = 1 deposits at weight td. Host-side only. Meow.
inline Footprint make_footprint(float w, float h, bool is_macro, float bin_w, float bin_h,
                                float target_density, bool clamp) {
    float cw = w, ch = h, weight = 1.0f;
    if (clamp) {
        const float SQRT2 = 1.41421356237309504880f;
        cw = w > bin_w * SQRT2 ? w : bin_w * SQRT2;
        ch = h > bin_h * SQRT2 ? h : bin_h * SQRT2;
        weight = (cw > 0.0f && ch > 0.0f) ? (w * h) / (cw * ch) : 0.0f;
        if (target_density < 1.0f && is_macro) weight = target_density;
    }
    return Footprint{0.5f * w, 0.5f * h, 0.5f * cw, 0.5f * ch, weight};
}

// A footprint the banked fast path can take: at most 2.99 bins wide on both axes, so it spans at most
// WINDOW_BINS bins wherever it sits (the 0.01-bin margin covers the bin-index rounding). Host-side only. Meow.
inline bool fits_window(const Footprint& fp, float bin_w, float bin_h) {
    return 2.0f * fp.half_cw <= 2.99f * bin_w && 2.0f * fp.half_ch <= 2.99f * bin_h;
}

} // namespace densrec

#endif // DENSITY_FOOTPRINT_HPP
