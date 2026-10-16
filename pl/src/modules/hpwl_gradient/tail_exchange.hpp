#ifndef PL_HPWL_GRADIENT_TAIL_EXCHANGE_HPP
#define PL_HPWL_GRADIENT_TAIL_EXCHANGE_HPP

// tail_exchange -- how chunks exchange tail slots (#47 B3; replaces the DDR mailbox). A borrower's
// tail slots (the packer's "externals") are its tail rows (packer rule C7), one block per owner on
// fresh rows. The owner writes a block's positions straight into the borrower's position image
// (send), and later adds the gradients the borrower drained into the same block of grad_DDR into
// its own (merge). The block's row-major order is not the owner's emission order, so both go through
// an on-chip row buffer, replaying a host-scheduled op list (pin_record.hpp TailBlockDesc) one op per
// cycle. The borrower side needs nothing: load and drain already cover the tail rows. Meow.

#include "format/pin_record.hpp"
#include "modules/hpwl_gradient/slot_arrays.hpp"   // ROWS_PER_BANK

namespace plalgo {

// Owner side, send: owned positions -> the borrower's tail block, row by row. An ENTRY parks one
// position at (its bank, that bank's next row); a TAIL_BEAT writes the next half-row out, sequentially
// from the block's first beat. Meow.
static void send_tail_block(const pinrec::TailBlockDesc& block, const uint32_t* tail_ops_DDR,
                            const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK], pinrec::SlotBeat* pos_DDR) {
    // static: a half-row of a block's last row can carry banks with nothing there (padding slots no
    // record reads); static keeps those values defined (zero, or an earlier block's) at no cost. Meow.
    static float row_buf_BRAM[pinrec::BANKS][pinrec::TAIL_BUFFER_ROWS];   // one bank per BRAM
#pragma HLS ARRAY_PARTITION variable=row_buf_BRAM complete dim=1
    int next_row_REG[pinrec::BANKS];
#pragma HLS ARRAY_PARTITION variable=next_row_REG complete dim=0
    for (int b = 0; b < pinrec::BANKS; b++) next_row_REG[b] = 0;
    int beat = 0;
    // Copy the positions one borrowing chunk needs from this owner's URAM into that chunk's tail in
    // DDR. The row buffer collects them until a whole 16-float beat is ready, so DDR is only ever
    // written in full beats, in order. Meow.
send_ops:
    for (int i = 0; i < block.num_send_ops; i++) {
#pragma HLS PIPELINE II=1
        const uint32_t op = tail_ops_DDR[block.send_op_offset + i];
        if (op & pinrec::TAIL_BEAT_OP) {
            const int half = beat & 1, row = (beat >> 1) & (pinrec::TAIL_BUFFER_ROWS - 1);
            pinrec::SlotBeat out;
            for (int j = 0; j < pinrec::LANES; j++) out.v[j] = row_buf_BRAM[half * pinrec::LANES + j][row];
            pos_DDR[block.block_beat_offset + beat] = out;
            beat++;
        } else {
            const uint32_t slot = pinrec::tail_entry_slot(op);
            const int bank = pinrec::tail_entry_bank(op);
            row_buf_BRAM[bank][next_row_REG[bank] & (pinrec::TAIL_BUFFER_ROWS - 1)] = pos_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS];
            next_row_REG[bank]++;
        }
    }
}

// Owner side, merge: the borrower's drained tail block -> += into owned gradients. A TAIL_BEAT loads
// the block's next half-row; an ENTRY adds (its bank, that bank's next row) into its owner slot. A
// block's owner slots are distinct and ascending (C7), so the read-add-write never meets itself
// inside a block, and the pipeline drains between blocks. Meow.
static void merge_tail_block(const pinrec::TailBlockDesc& block, const uint32_t* tail_ops_DDR,
                             const pinrec::SlotBeat* grad_DDR, float grad_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
    static float row_buf_BRAM[pinrec::BANKS][pinrec::TAIL_BUFFER_ROWS];
#pragma HLS ARRAY_PARTITION variable=row_buf_BRAM complete dim=1
    int next_row_REG[pinrec::BANKS];
#pragma HLS ARRAY_PARTITION variable=next_row_REG complete dim=0
    for (int b = 0; b < pinrec::BANKS; b++) next_row_REG[b] = 0;
    int beat = 0;
    // The reverse of send_ops: read, in full beats, the gradients one borrowing chunk computed for
    // this owner's nodes, and add each into the owner's gradient for that node. Meow.
merge_ops:
    for (int i = 0; i < block.num_merge_ops; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM inter false
        const uint32_t op = tail_ops_DDR[block.merge_op_offset + i];
        if (op & pinrec::TAIL_BEAT_OP) {
            const int half = beat & 1, row = (beat >> 1) & (pinrec::TAIL_BUFFER_ROWS - 1);
            const pinrec::SlotBeat in = grad_DDR[block.block_beat_offset + beat];
            for (int j = 0; j < pinrec::LANES; j++) row_buf_BRAM[half * pinrec::LANES + j][row] = in.v[j];
            beat++;
        } else {
            const uint32_t slot = pinrec::tail_entry_slot(op);
            const int bank = pinrec::tail_entry_bank(op);
            grad_URAM[slot % pinrec::BANKS][slot / pinrec::BANKS] += row_buf_BRAM[bank][next_row_REG[bank] & (pinrec::TAIL_BUFFER_ROWS - 1)];
            next_row_REG[bank]++;
        }
    }
}

} // namespace plalgo

#endif // PL_HPWL_GRADIENT_TAIL_EXCHANGE_HPP
