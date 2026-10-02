#ifndef PL_ALGO_HPWL_COMPUTER_V3_HPP
#define PL_ALGO_HPWL_COMPUTER_V3_HPP

// hpwl_computer_v3 -- hpwl_computer_v2 extended to designs larger than one on-chip slot space (#41).
//
// The host splits the design into chunks (beat_packer.hpp encode_chunked): each chunk owns some
// nodes, carries the nets homed in it as an ordinary record stream over its own local slots, and
// holds EXTERNAL slots for nodes owned by other chunks. External positions travel through one DDR
// mailbox, consumer-major (chunk k's inbox = its external slots, one parcel per owner).
//
//   send pass      for each chunk j: load its positions, refresh its macro pins, send the
//                  positions other chunks need into their inboxes (one sequential parcel each)
//   compute pass   for each chunk k: load its positions, refresh, read its inbox sequentially
//                  into its external slots, then run hpwl_computer_v2's beat loop on its stream
//
// Every DDR access is still sequential -- parcels, inboxes, descriptors, streams -- and every
// random access is still on chip. One chunk means nothing to send, so the send pass is
// skipped and this is exactly hpwl_computer_v2. Meow.

#include "modules/hpwl_computer_v2.hpp"

namespace plalgo {

// Owner side: this chunk's positions that other chunks hold as external, segment by segment.
// Padding entries (slot -1, only there to space the gradient return) write nothing. Meow.
static void send_external_positions(const pinrec::ChunkDesc& desc,
                                    const pinrec::ParcelRef* parcels_DDR, const int32_t* shared_slots_DDR,
                                    const float src_URAM[pinrec::BANKS][ROWS_PER_BANK], float* mailbox_DDR) {
    int entry = desc.shared_list_offset;
send_parcels:
    for (int k = 0; k < desc.num_parcels; k++) {
        const pinrec::ParcelRef parcel = parcels_DDR[desc.parcel_offset + k];
    send_parcel:
        for (int i = 0; i < parcel.count; i++) {
#pragma HLS PIPELINE II=1
            const int32_t slot = shared_slots_DDR[entry + i];
            if (slot < 0) continue;
            mailbox_DDR[parcel.offset + i] = src_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS];
        }
        entry += parcel.count;
    }
}

// Consumer side: this chunk's inbox, in order, into its external slots (all distinct). Meow.
static void receive_external_positions(const pinrec::ChunkDesc& desc, const int32_t* external_slots_DDR,
                                       const float* mailbox_DDR, float dst_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
receive_externals:
    for (int i = 0; i < desc.inbox_size; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=dst_URAM inter false
        const int32_t slot = external_slots_DDR[desc.external_list_offset + i];
        dst_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS] = mailbox_DDR[desc.inbox_offset + i];
    }
}

// This chunk's GROUP_COUNTS entries: the degree-group counts, then the large-net span counts. Meow.
static void load_group_counts(const int32_t* group_counts_DDR, int beat_count_REG[pinrec::NET_DEGREES_PROCESSED],
                              int span_count_REG[pinrec::SPAN_GROUPS]) {
load_group_counts:
    for (int k = 0; k < pinrec::GROUP_COUNTS; k++) {
#pragma HLS PIPELINE II=1
        const int count = group_counts_DDR[k];
        if (k < pinrec::NET_DEGREES_PROCESSED) beat_count_REG[k] = count;
        else                                   span_count_REG[k - pinrec::NET_DEGREES_PROCESSED] = count;
    }
}

static void hpwl_computer_v3(
        const pinrec::ChunkDesc*        chunks_DDR,        // [num_chunks]
        const int32_t*                  group_counts_DDR,  // [num_chunks * GROUP_COUNTS]
        int                             num_chunks,
        const pinrec::RecordBeat*       records_DDR,       // every chunk's stream, concatenated, this axis
        const pinrec::SlotBeat*         pos_DDR,           // every chunk's slot-major positions, this axis
        const pinrec::MacroPinRef*      macro_pins_DDR,    // every chunk's refresh list, this axis
        const int32_t*                  external_slots_DDR, // every chunk's external slots, inbox order
        const int32_t*                  shared_slots_DDR,  // every chunk's shared own slots, parcel order
        const pinrec::ParcelRef*        parcels_DDR,       // each chunk's segments (num_parcels)
        float*                          mailbox_DDR,       // scratch: external positions, consumer-major
        const float*                    offset_table_DDR,  // [offset_table_size] shared by all chunks
        int                             offset_table_size,
        OutBeat*                        out_beats_DDR,     // per-net HPWL, same layout as records_DDR
        int                             offset_bits) {

    ONCHIP_ARRAY float pos_URAM[pinrec::BANKS][ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=pos_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=pos_URAM type=ram_2p impl=uram
    ONCHIP_ARRAY float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX];
#pragma HLS ARRAY_PARTITION variable=offset_BRAM complete dim=1
    int beat_count_REG[pinrec::NET_DEGREES_PROCESSED];
#pragma HLS ARRAY_PARTITION variable=beat_count_REG complete dim=0
    int span_count_REG[pinrec::SPAN_GROUPS];
#pragma HLS ARRAY_PARTITION variable=span_count_REG complete dim=0

    load_offset_table(offset_table_DDR, offset_table_size, offset_BRAM);

send_pass:
    for (int j = 0; j < num_chunks && num_chunks > 1; j++) {
        const pinrec::ChunkDesc desc = chunks_DDR[j];
        load_slot_array(pos_DDR + desc.slot_beat_offset, desc.num_slot_beats, pos_URAM);
        refresh_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, pos_URAM);
        send_external_positions(desc, parcels_DDR, shared_slots_DDR, pos_URAM, mailbox_DDR);
    }

compute_pass:
    for (int k = 0; k < num_chunks; k++) {
        const pinrec::ChunkDesc desc = chunks_DDR[k];
        load_slot_array(pos_DDR + desc.slot_beat_offset, desc.num_slot_beats, pos_URAM);
        refresh_macro_pins(macro_pins_DDR + desc.macro_pin_offset, desc.num_macro_pins, pos_URAM);
        receive_external_positions(desc, external_slots_DDR, mailbox_DDR, pos_URAM);
        load_group_counts(group_counts_DDR + k * pinrec::GROUP_COUNTS, beat_count_REG, span_count_REG);
        hpwl_beat_loop(records_DDR + desc.record_beat_offset, desc.num_beats, beat_count_REG, span_count_REG, pos_URAM,
                       offset_BRAM, out_beats_DDR + desc.record_beat_offset, offset_bits);
    }
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_COMPUTER_V3_HPP
