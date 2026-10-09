#ifndef PIN_RECORD_HPP
#define PIN_RECORD_HPP

// pin_record.hpp -- the host->device pin-record protocol (#41). Shared by the host packer
// (beat_packer.hpp) and every device module that consumes the stream, so neither side can drift.
// Plain C++ with fixed-width types only: it compiles under g++ and Vitis HLS alike.
// Copied 2026-10-06 (#45) from vck5000/bring_up/beat_packer/pin_record.hpp @ 41f2193; this is
// now the live copy, the vck5000 one is read-only legacy. Meow.
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
// Large nets (LANES+1..MAX_LARGE_NET_DEGREE pins) follow the degree-16 group, one net per beat over
// `span` consecutive beats, grouped by span (2..MAX_SPAN) so the device finds a net's last beat
// with a counter. The span is what bank packing needs, which can exceed ceil(degree/LANES); nets
// needing more than MAX_SPAN beats are dropped on the host; nets over 96 pins are masked at read
// time (the host's IGNORE_NET_DEGREE, #46). Meow.
constexpr int      MAX_LARGE_NET_DEGREE  = 6 * LANES;                     // 96
constexpr int      MAX_SPAN              = 8;
constexpr int      SPAN_GROUPS           = MAX_SPAN - 1;                  // spans 2..MAX_SPAN
// A large net's extent: stream positions from its first real beat to its last, pads inside
// included (packer rule L8). The gradient's stage FIFOs hold one extent (plus pipeline skew) while
// a later stage waits on the net's bbox / sums, so this bounds their depth. Measured max over the 44 designs: 16
// (mms/newblue3, split nodes); every other design <= 7. Meow.
constexpr int      MAX_NET_EXTENT        = 16;
constexpr int      BANK_BITS             = 5;
constexpr int      BANKS                 = 1 << BANK_BITS;                // 32 URAM banks per array
constexpr uint32_t EMPTY_RECORD          = 0xFFFFFFFFu;
// Beats between two updates of one node's gradient: the scatter-add read-add-write round trip.
// The host schedules to it; the device declares it to HLS as the true dependence distance. Meow.
constexpr int      HAZARD_DISTANCE       = 4;

// The device's total HPWL (one axis) is an int64 sum of each net's float HPWL in units of
// 2^-HPWL_FRAC_BITS. Integer addition is associative, so the total is bit-exact whatever the
// chunking or summation order, which a float or double sum is not; it feeds the lambda schedule,
// a control path. Range 2^39 (5.5e11) per axis; quantization <= 2^-24 per net. Meow.
constexpr int      HPWL_FRAC_BITS        = 24;
// Truncates; exact for net_hpwl >= 1. Equals (int64_t)(net_hpwl * 2^HPWL_FRAC_BITS) for every finite
// net_hpwl in [0, 2^39), but scaling a float by a power of two only moves its exponent, so the
// mantissa is shifted into place directly: a 64-bit barrel shift, where the multiply cost a DSP per
// net (8 per beat). Denormals are < 2^-126 and truncate to 0. Meow.
inline int64_t hpwl_to_fixed(float net_hpwl) {
    union { float f; uint32_t u; } bits;
    bits.f = net_hpwl;
    const int     biased_exponent = (bits.u >> 23) & 0xFF;
    const int64_t mantissa        = (bits.u & 0x7FFFFF) | 0x800000;   // hidden bit restored
    const int     shift           = biased_exponent - 150 + HPWL_FRAC_BITS;   // 150 = bias 127 + 23 mantissa bits
    if (biased_exponent == 0 || shift < -23) return 0;
    return shift >= 0 ? mantissa << shift : mantissa >> -shift;
}

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
// in DDR and located by this descriptor. External slots (nodes owned by another chunk) are the
// chunk's TAIL rows (packer rule C7): one block per owner, each starting on a fresh row. The owner
// fills its block in the consumer's position image and later adds the gradients the consumer drained
// there into its own, through a TailStream (#47). See beat_packer.hpp (encode_chunked).
// A chunk's group ends (degree groups, then large-net span groups) are a separate int32 table, NUM_GROUP_ENDS
// per chunk: inside ChunkDesc they would make it 1120 bits, past the 1024-bit m_axi maximum. Meow.
constexpr int NUM_GROUP_ENDS = NET_DEGREES_PROCESSED + SPAN_GROUPS;   // 15 degree + 7 span group ends
struct ChunkDesc {
    int32_t record_beat_offset, num_beats;          // into the concatenated record stream
    int32_t slot_beat_offset, num_slot_beats;       // into the concatenated slot-major images
    int32_t first_tail_slot;                        // owned movable slots below, the tail rows from here
    int32_t first_fixed_slot;
    int32_t macro_pin_offset, num_macro_pins;       // into the concatenated macro-pin lists
    int32_t stream_offset, num_streams;             // this chunk's tail streams AS OWNER, consumers ascending
};

// One owner -> consumer block of the consumer's tail, exchanged through an on-chip row buffer of
// BANKS banks x TAIL_BUFFER_ROWS rows. Each list is replayed in order, one op per cycle; an ENTRY's
// row is the next one of its bank (rows within a block fill bank by bank, in emission order):
//   send  (owner's send pass) : ENTRY     buffer[bank][row] = pos[owner slot]
//                               TAIL_BEAT the buffer's next half-row -> the block, in pos_DDR
//   merge (owner's fold pass) : TAIL_BEAT the block's next half-row, from grad_DDR -> the buffer
//                               ENTRY     grad[owner slot] += buffer[bank][row]
// The host orders the ops so no buffer row is overwritten before it is written out (send) or used
// (merge). A block's entries name distinct owner slots, ascending. Meow.
struct TailStream {
    int32_t send_op_offset, num_send_ops;
    int32_t merge_op_offset, num_merge_ops;         // into the concatenated tail ops
    int32_t block_beat_offset;                      // the block's first beat in the concatenated slot images
};

#ifndef PL_TAIL_BUFFER_ROWS
#define PL_TAIL_BUFFER_ROWS 256   // override (-D) only for experiments; the host schedules against it
#endif
constexpr int      TAIL_BUFFER_ROWS   = PL_TAIL_BUFFER_ROWS;   // a power of 2
constexpr uint32_t TAIL_BEAT_OP       = 1u << 31;
constexpr int      TAIL_OP_BANK_SHIFT = 25;                     // ENTRY: owner slot (25 bits) | bank << 25
inline uint32_t make_tail_entry(uint32_t owner_slot, int bank) { return owner_slot | (uint32_t)bank << TAIL_OP_BANK_SHIFT; }
inline uint32_t tail_entry_slot(uint32_t op) { return op & ((1u << TAIL_OP_BANK_SHIFT) - 1u); }
inline int      tail_entry_bank(uint32_t op) { return (int)(op >> TAIL_OP_BANK_SHIFT) & (BANKS - 1); }

inline uint32_t record_node_slot(uint32_t record, int offset_bits)  { return record >> offset_bits; }
inline uint32_t record_offset_idx(uint32_t record, int offset_bits) { return record & ((1u << offset_bits) - 1u); }
inline uint32_t make_record(uint32_t node_slot, uint32_t offset_idx, int offset_bits) {
    return node_slot << offset_bits | offset_idx;
}
// Largest node_slot a real node may take for this split (the field maximum is EMPTY).
inline uint32_t max_real_node_slot(int offset_bits) { return (0xFFFFFFFFu >> offset_bits) - 1u; }

} // namespace pinrec

#endif // PIN_RECORD_HPP
