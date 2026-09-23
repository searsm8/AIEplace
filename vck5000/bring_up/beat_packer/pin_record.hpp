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
// chip each call; its gradient folds back into the macro as a read-add-write, so the host orders
// the list with no macro repeated within HAZARD_DISTANCE entries (padding with SKIP entries when a
// macro dominates). FIRST starts a macro's sum instead of adding to it; LAST writes the total into
// the macro's gradient -- no separate zeroing or copy pass. 16 bytes: a 12-byte entry straddles
// bus words and cost refresh_macros II=2. Meow.
constexpr int32_t MACRO_PIN_SKIP  = -1;   // pin_slot of a padding entry
constexpr int32_t MACRO_PIN_FIRST = 1;
constexpr int32_t MACRO_PIN_LAST  = 2;
struct MacroPinRef {
    int32_t pin_slot;
    int32_t macro_slot;
    float   offset;      // this axis
    int32_t flags;       // MACRO_PIN_FIRST | MACRO_PIN_LAST
};

// ---- Chunking (a design larger than one on-chip slot space) ----
// Each chunk is an ordinary stream over its own local slots; the per-chunk arrays are concatenated
// in DDR and located by this descriptor. Ghost slots (nodes owned by another chunk) are filled
// from, and return their gradients to, one DDR exchange buffer laid out consumer-major: chunk k's
// region holds its ghosts as one block per producer. See beat_packer.hpp (encode_chunked). Meow.
struct ChunkDesc {
    int32_t record_beat_offset, num_beats;          // into the concatenated record stream
    int32_t beat_count[NET_DEGREES_PROCESSED];      // this chunk's degree groups
    int32_t slot_beat_offset, num_slot_beats;       // into the concatenated slot-major images
    int32_t first_fixed_slot;
    int32_t macro_pin_offset, num_macro_pins;       // into the concatenated macro-pin lists
    int32_t import_region, num_imports;             // this chunk's region in the exchange buffer
    int32_t import_list_offset;                     // into the concatenated ghost-slot lists
    int32_t export_list_offset;                     // into the concatenated own-slot export lists
    int32_t export_block_offset;                    // into the block table: num_chunks entries
};

struct ExchangeBlockRef { int32_t offset, count; };   // a producer's block within a consumer region

inline uint32_t record_node_slot(uint32_t record, int offset_bits)  { return record >> offset_bits; }
inline uint32_t record_offset_idx(uint32_t record, int offset_bits) { return record & ((1u << offset_bits) - 1u); }
inline uint32_t make_record(uint32_t node_slot, uint32_t offset_idx, int offset_bits) {
    return node_slot << offset_bits | offset_idx;
}
// Largest node_slot a real node may take for this split (the field maximum is EMPTY).
inline uint32_t max_real_node_slot(int offset_bits) { return (0xFFFFFFFFu >> offset_bits) - 1u; }

} // namespace pinrec

#endif // PIN_RECORD_HPP
