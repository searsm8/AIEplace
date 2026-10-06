#ifndef PL_HPWL_GRADIENT_MAILBOX_HPP
#define PL_HPWL_GRADIENT_MAILBOX_HPP

// mailbox -- how chunks exchange external slots through one DDR buffer, laid out consumer-major
// (chunk k's inbox = its external slots, one parcel per owner). Positions go owner -> consumer
// (send, receive); gradients come back consumer -> owner (return, collect). The same buffer
// carries both: chunk k's inbox is read (positions) before it is written (gradients).
// From vck5000/bring_up/hpwl_computer_v3 and hpwl_gradient_computer_v2 (modules/*.hpp) @ 41f2193 (#45). Meow.

#include "format/pin_record.hpp"
#include "modules/hpwl_gradient/slot_arrays.hpp"   // ROWS_PER_BANK

namespace plalgo {

// Padding segments (ParcelRef offset -1) are skipped here, in the outer loop, never entry by entry:
// a real segment holds no padding (packer contract C3, check_chunked), so the inner loop's mailbox
// access is unconditional and HLS bursts it. Behind a per-entry `if (slot < 0)` it was single
// requests, ~4.3 cycles each (#42 option A). Meow.
static bool is_padding(const pinrec::ParcelRef& parcel) { return parcel.offset < 0; }

// Owner side: this chunk's positions that other chunks hold as external, segment by segment.
// Padding segments (only there to space the gradient collect) send nothing. Meow.
static void send_external_positions(const pinrec::ChunkDesc& desc,
                                    const pinrec::ParcelRef* parcels_DDR, const int32_t* shared_slots_DDR,
                                    const float src_URAM[pinrec::BANKS][ROWS_PER_BANK], float* mailbox_DDR) {
    int entry = desc.shared_list_offset;
send_parcels:
    for (int k = 0; k < desc.num_parcels; k++) {
        const pinrec::ParcelRef parcel = parcels_DDR[desc.parcel_offset + k];
        if (!is_padding(parcel)) {
        send_parcel:
            for (int i = 0; i < parcel.count; i++) {
#pragma HLS PIPELINE II=1
                const int32_t slot = shared_slots_DDR[entry + i];
                mailbox_DDR[parcel.offset + i] = src_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS];
            }
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

// Consumer side, reversed: this chunk's external slots' gradients, in inbox order. Meow.
static void return_external_gradients(const pinrec::ChunkDesc& desc, const int32_t* external_slots_DDR,
                                      const float grad_URAM[pinrec::BANKS][ROWS_PER_BANK], float* mailbox_DDR) {
return_gradients:
    for (int i = 0; i < desc.inbox_size; i++) {
#pragma HLS PIPELINE II=1
        const int32_t slot = external_slots_DDR[desc.external_list_offset + i];
        mailbox_DDR[desc.inbox_offset + i] = grad_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS];
    }
}

// Owner side, reversed: add every consumer's gradient for this chunk's shared nodes. The host keeps
// a slot HAZARD_DISTANCE entries apart within a segment; across segments the spacing comes from
// the pipeline draining between two invocations of collect_parcel (no rewind), which is why
// skipping a padding segment is safe -- RTL co-sim with a negative control is what checks that. Meow.
static void collect_external_gradients(const pinrec::ChunkDesc& desc,
                                       const pinrec::ParcelRef* parcels_DDR, const int32_t* shared_slots_DDR,
                                       const float* mailbox_DDR, float grad_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
    int entry = desc.shared_list_offset;
collect_parcels:
    for (int k = 0; k < desc.num_parcels; k++) {
        const pinrec::ParcelRef parcel = parcels_DDR[desc.parcel_offset + k];
        if (!is_padding(parcel)) {
        collect_parcel:
            for (int i = 0; i < parcel.count; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM type=inter direction=RAW distance=pinrec::HAZARD_DISTANCE dependent=true
                const int32_t slot = shared_slots_DDR[entry + i];
                grad_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS] += mailbox_DDR[parcel.offset + i];
            }
        }
        entry += parcel.count;
    }
}

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_MAILBOX_HPP
