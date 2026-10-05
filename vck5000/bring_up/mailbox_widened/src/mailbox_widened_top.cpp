// mailbox_widened_top -- C-synthesis wrapper for the #42 widened mailbox loops: does every loop
// schedule at II=1 against 32 URAM banks, and what does the bank-major crossbar cost? One chunk's
// view of all four loops: load, receive, send, return, collect, drain. Not a kernel we ship. Meow.

#include "modules/mailbox_widened.hpp"

extern "C" void mailbox_widened_top(const pinrec::ParcelRef* parcels, int num_parcels,
                                    const plalgo::LaneBeat* shared_lanes, const plalgo::LaneBeat* external_lanes,
                                    pinrec::SlotBeat* mailbox, int inbox_offset, int inbox_beats,
                                    const pinrec::SlotBeat* slots_in, int num_slot_beats, pinrec::SlotBeat* grad_out) {
#pragma HLS INTERFACE m_axi port=parcels        bundle=gmem0 offset=slave
#pragma HLS INTERFACE m_axi port=shared_lanes   bundle=gmem1 offset=slave
#pragma HLS INTERFACE m_axi port=external_lanes bundle=gmem2 offset=slave
#pragma HLS INTERFACE m_axi port=mailbox        bundle=gmem3 offset=slave
#pragma HLS INTERFACE m_axi port=slots_in       bundle=gmem4 offset=slave
#pragma HLS INTERFACE m_axi port=grad_out       bundle=gmem5 offset=slave
#pragma HLS INTERFACE s_axilite port=return

    ONCHIP_ARRAY float pos_URAM[pinrec::BANKS][plalgo::ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=pos_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=pos_URAM type=ram_2p impl=uram
    ONCHIP_ARRAY float grad_URAM[pinrec::BANKS][plalgo::ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=grad_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=grad_URAM type=ram_2p impl=uram

    plalgo::load_slot_array(slots_in, num_slot_beats, pos_URAM);
    plalgo::load_slot_array(slots_in, num_slot_beats, grad_URAM);
    plalgo::receive_wide(external_lanes, mailbox + inbox_offset, inbox_beats, pos_URAM);
    plalgo::send_wide(parcels, num_parcels, shared_lanes, pos_URAM, mailbox);
    plalgo::return_wide(external_lanes, grad_URAM, mailbox + inbox_offset, inbox_beats);
    plalgo::collect_wide(parcels, num_parcels, shared_lanes, mailbox, grad_URAM);
    plalgo::drain_slot_array(grad_URAM, num_slot_beats, grad_out);
}
