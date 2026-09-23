#ifndef PIN_RECORD_HPP
#define PIN_RECORD_HPP

// pin_record.hpp -- the host->device pin-record protocol (#41). Shared by the host packer
// (beat_packer.hpp) and every device module that consumes the stream, so neither side can drift.
// Plain C++ with fixed-width types only: it compiles under g++ and Vitis HLS alike.
//
//     record     = node_slot << offset_bits | offset_idx          (32 bits, 16 per 512-bit beat)
//     node_slot  = row * BANKS + bank    -> bank = node_slot % BANKS, URAM row = node_slot / BANKS
//     offset_idx = index into this axis's on-chip table of distinct pin offsets
//
// The node field takes every bit above offset_bits, so offset_bits (a per-design register) is the
// only split parameter. EMPTY_RECORD (all ones) is a lane with no pin: its node field is the
// field's maximum, which the host never assigns to a real node. Meow.

#include <cstdint>

namespace pinrec {

constexpr int      LANES                 = 16;   // pins per beat
constexpr int      MIN_NET_DEGREE        = 2;
constexpr int      NET_DEGREES_PROCESSED = LANES - MIN_NET_DEGREE + 1;   // degrees 2..16
constexpr int      MAX_NETS_PER_BEAT     = LANES / MIN_NET_DEGREE;       // 8
constexpr int      BANK_BITS             = 5;
constexpr int      BANKS                 = 1 << BANK_BITS;                // 32 URAM banks per array
constexpr uint32_t EMPTY_RECORD          = 0xFFFFFFFFu;
// Beats between two updates of one node's gradient: the scatter-add read-add-write round trip.
// The host schedules to it; the device declares it to HLS as the true dependence distance. Meow.
constexpr int      HAZARD_DISTANCE       = 4;

struct RecordBeat { uint32_t r[LANES]; };   // one 512-bit DDR beat of the static stream
struct SlotBeat   { float    v[LANES]; };   // one 512-bit DDR beat of a slot-major array

// A movable macro's pin is its own slot: pos[pin_slot] = pos[macro_slot] + offset, refreshed on
// chip each call; its gradient folds back into the macro. Entries are grouped by macro_slot. Meow.
struct MacroPinRef {
    int32_t pin_slot;
    int32_t macro_slot;
    float   offset;      // this axis
};

inline uint32_t record_node_slot(uint32_t record, int offset_bits)  { return record >> offset_bits; }
inline uint32_t record_offset_idx(uint32_t record, int offset_bits) { return record & ((1u << offset_bits) - 1u); }
inline uint32_t make_record(uint32_t node_slot, uint32_t offset_idx, int offset_bits) {
    return node_slot << offset_bits | offset_idx;
}
// Largest node_slot a real node may take for this split (the field maximum is EMPTY).
inline uint32_t max_real_node_slot(int offset_bits) { return (0xFFFFFFFFu >> offset_bits) - 1u; }

} // namespace pinrec

#endif // PIN_RECORD_HPP
