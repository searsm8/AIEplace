// Verify the host<->device pin-record contract (src/format/pin_record.hpp) holds its own invariants.
// Every module and the packer build on these, so a careless constant edit should fail here first.
//
//   [1] LAYOUT     the DDR structs are the widths the m_axi ports assume
//   [2] CONSTANTS  derived constants agree with what they are derived from
//   [3] ROUND TRIP make_record -> record_node_slot / record_offset_idx, every offset_bits split
//   [4] EMPTY      no real record equals EMPTY_RECORD; the node field maximum does. Meow.

#include "format/pin_record.hpp"

#include <cstdio>
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
    check(sizeof(ParcelRef) == 8, "[1] ParcelRef is 8 bytes");

    check(BANKS == 1 << BANK_BITS, "[2] BANKS == 2^BANK_BITS");
    check(NET_DEGREES_PROCESSED == 15 && MAX_NETS_PER_BEAT == 8, "[2] degrees 2..16, 8 nets per beat");
    check(GROUP_COUNTS == NET_DEGREES_PROCESSED + SPAN_GROUPS, "[2] GROUP_COUNTS = degree + span groups");
    check(MAX_LARGE_NET_DEGREE % LANES == 0, "[2] the largest net fills whole beats");
    check(MAX_LARGE_NET_DEGREE / LANES <= MAX_SPAN, "[2] the largest net's minimum span is within MAX_SPAN");
    check(MAX_NET_EXTENT >= MAX_SPAN, "[2] a net's extent bound covers its maximum span");

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

    if (failures) { printf("FAIL: pin_record (%d checks)\n", failures); return 1; }
    printf("PASS: pin_record\n");
    return 0;
}
