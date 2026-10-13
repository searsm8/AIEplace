#ifndef DENSITY_FOOTPRINT_HPP
#define DENSITY_FOOTPRINT_HPP

// density_footprint.hpp -- the host->device contract for density_map_CU. Shared by the host packer and
// the device, so neither side can drift. Plain C++ with fixed-width types: g++ and Vitis HLS alike.
//
// Per slot, the device reads its position (the gradient's slot-major pos arrays, lower-left corner,
// die frame) and a footprint index: 16 per 512-bit beat, aligned beat-for-beat with the positions.
// The index selects an entry of a small on-chip table of distinct footprints, the way a pin record's
// offset_idx selects a pin offset (pin_record.hpp): cells come from a library of few sizes, and every
// filler shares one entry. Slots that deposit nothing (EXTERNAL copies, pin slots, EMPTY padding)
// carry NO_DEPOSIT. Fixed nodes are not in the stream: the host deposits them once into a base map,
// already capped at min(rho, td) (#35), which the device loads as the accumulator's starting value. Meow.

#include "format/pin_record.hpp"

#include <cstdint>

namespace densrec {

constexpr int      GRID_MAX            = 1024;          // v1 hardware grid; 2048 is the planned extension
constexpr int      FOOTPRINT_TABLE_MAX = 4096;          // assumed, not yet measured on the 44 designs
constexpr uint32_t NO_DEPOSIT          = 0xFFFFFFFFu;

struct FootprintBeat { uint32_t idx[pinrec::LANES]; };  // one 512-bit DDR beat of footprint indices

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

} // namespace densrec

#endif // DENSITY_FOOTPRINT_HPP
