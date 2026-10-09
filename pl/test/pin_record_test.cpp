// Verify the host<->device pin-record contract (src/format/pin_record.hpp) holds its own invariants.
// Every module and the packer build on these, so a careless constant edit should fail here first.
//
//   [1] LAYOUT     the DDR structs are the widths the m_axi ports assume
//   [2] CONSTANTS  derived constants agree with what they are derived from
//   [3] ROUND TRIP make_record -> record_node_slot / record_offset_idx, every offset_bits split
//   [4] EMPTY      no real record equals EMPTY_RECORD; the node field maximum does.
//   [5] HPWL FIXED hpwl_to_fixed (shift form) equals the truncated multiply, exhaustively. Meow.

#include "format/pin_record.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace pinrec;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

int main() {
    check(sizeof(RecordBeat) * 8 == 512, "[1] RecordBeat is one 512-bit beat");
    check(sizeof(SlotBeat) * 8 == 512, "[1] SlotBeat is one 512-bit beat");
    check(sizeof(MacroPinRef) == 16, "[1] MacroPinRef is 16 bytes (12 straddled bus words: II=2)");
    check(sizeof(ChunkDesc) * 8 <= 512, "[1] ChunkDesc fits one 512-bit port word");
    check(sizeof(TailStream) == 20, "[1] TailStream is 20 bytes");

    check(BANKS == 1 << BANK_BITS, "[2] BANKS == 2^BANK_BITS");
    check(NET_DEGREES_PROCESSED == 15 && MAX_NETS_PER_BEAT == 8, "[2] degrees 2..16, 8 nets per beat");
    check(NUM_GROUP_ENDS == NET_DEGREES_PROCESSED + SPAN_GROUPS, "[2] NUM_GROUP_ENDS = degree + span groups");
    check(MAX_LARGE_NET_DEGREE % LANES == 0, "[2] the largest net fills whole beats");
    check(MAX_LARGE_NET_DEGREE / LANES <= MAX_SPAN, "[2] the largest net's minimum span is within MAX_SPAN");
    check(MAX_NET_EXTENT >= MAX_SPAN, "[2] a net's extent bound covers its maximum span");
    check((TAIL_BUFFER_ROWS & (TAIL_BUFFER_ROWS - 1)) == 0, "[2] TAIL_BUFFER_ROWS is a power of 2");
    {   // A tail ENTRY carries any slot of a chunk and any bank, and never looks like a TAIL_BEAT. Meow.
        bool tail_ok = true;
        for (uint32_t slot : {0u, 1u, (1u << 20) - 1, (1u << TAIL_OP_BANK_SHIFT) - 1})
            for (int bank : {0, 1, BANKS - 1}) {
                const uint32_t op = make_tail_entry(slot, bank);
                tail_ok &= tail_entry_slot(op) == slot && tail_entry_bank(op) == bank && !(op & TAIL_BEAT_OP);
            }
        check(tail_ok, "[2] tail ENTRY ops round-trip slot and bank, and are not TAIL_BEAT");
    }

    bool round_trip_ok = true, empty_ok = true;
    for (int offset_bits = 1; offset_bits <= 16; offset_bits++) {
        const uint32_t max_slot = max_real_node_slot(offset_bits);
        const uint32_t max_offset = (1u << offset_bits) - 1u;
        for (uint32_t node_slot : {0u, 1u, max_slot / 2, max_slot})
            for (uint32_t offset_idx : {0u, max_offset / 2, max_offset}) {
                const uint32_t record = make_record(node_slot, offset_idx, offset_bits);
                if (record_node_slot(record, offset_bits) != node_slot ||
                    record_offset_idx(record, offset_bits) != offset_idx) round_trip_ok = false;
                if (record == EMPTY_RECORD) empty_ok = false;
            }
        if (make_record(max_slot + 1, max_offset, offset_bits) != EMPTY_RECORD) empty_ok = false;
    }
    check(round_trip_ok, "[3] record round-trips for offset_bits 1..16");
    check(empty_ok, "[4] EMPTY_RECORD is the node-field maximum and no real record");

    // The shift form must equal the multiply it replaced, on every float the device can see:
    // all non-negative finite values below the 2^39 range, exhaustively (~1.4e9). x * 2^24 is
    // exact in double, so the cast is the truncation the definition names. Meow.
    long fixed_mismatches = 0;
    const uint32_t range_end_bits = (uint32_t)(127 + 39) << 23;   // bit pattern of 2^39
    for (uint32_t u = 0; u < range_end_bits; u++) {
        float x;
        std::memcpy(&x, &u, sizeof x);
        fixed_mismatches += hpwl_to_fixed(x) != (int64_t)((double)x * (double)(1 << HPWL_FRAC_BITS));
    }
    check(fixed_mismatches == 0, "[5] hpwl_to_fixed == truncated x * 2^HPWL_FRAC_BITS for every float in [0, 2^39)");

    if (failures) { printf("FAIL: pin_record (%d checks)\n", failures); return 1; }
    printf("PASS: pin_record\n");
    return 0;
}
