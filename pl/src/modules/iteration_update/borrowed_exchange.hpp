#ifndef PL_ITERATION_UPDATE_BORROWED_EXCHANGE_HPP
#define PL_ITERATION_UPDATE_BORROWED_EXCHANGE_HPP

// borrowed_exchange -- #47 B4 = A: the borrowed copies exchanged inside iteration_update_CU's stream of an
// owner's shared beats, one fused op per cycle (pin_record.hpp FUSED_*). Replaces hpwl_gradient_CU's
// send_borrowed_pass / merge_borrowed_pass, which reloaded the shared rows a second and third time.
//
// Each of the owner's borrowers s has a block in its borrowed rows (packer rule C7) and a ring of
// BORROWED_BUFFER_ROWS rows here, one BRAM per (s, axis, bank). A GROUP moves up to 4 entries between
// the owner beat's lanes and the ring, each entry the next row of its bank; the host puts a group's
// entries in distinct banks, so each bank is addressed by at most one entry. A BEAT moves one
// half-row between the ring and the block in DDR.
//
// These are op steps, inlined into iteration_update_CU's one pipelined loop, not stream processes: in a
// DATAFLOW region the merge would read grad DDR (the blocks) while the update writes it (the owned
// rows), one argument in two processes, which HLS's dataflow check rejects. Meow.

#include "format/pin_record.hpp"

namespace plalgo {

constexpr int MAX_BORROWERS = pinrec::FUSED_MAX_BORROWERS;
constexpr int RING_ROWS     = pinrec::BORROWED_BUFFER_ROWS;

struct FusedOp {
    bool beat, boundary;
    int  borrower;
    bool valid[pinrec::FUSED_GROUP_ENTRIES];
    int  lane[pinrec::FUSED_GROUP_ENTRIES];
    int  bank[pinrec::FUSED_GROUP_ENTRIES];
};

static FusedOp decode_fused_op(uint64_t op) {
#pragma HLS INLINE
    FusedOp d;
    d.beat     = (op & pinrec::FUSED_BEAT) != 0;
    d.boundary = (op & pinrec::FUSED_BOUNDARY) != 0;
    d.borrower = pinrec::fused_borrower(op);
    for (int e = 0; e < pinrec::FUSED_GROUP_ENTRIES; e++) {
        d.valid[e] = !d.beat && pinrec::fused_entry_valid(op, e);
        d.lane[e]  = pinrec::fused_entry_lane(op, e);
        d.bank[e]  = pinrec::fused_entry_bank(op, e);
    }
    return d;
}

// The entry (if any) addressing bank b: one 4-way compare per bank, so each bank sees one access. Meow.
static int entry_for_bank(const FusedOp& op, int b) {
#pragma HLS INLINE
    int hit = -1;
    for (int e = 0; e < pinrec::FUSED_GROUP_ENTRIES; e++) if (op.valid[e] && op.bank[e] == b) hit = e;
    return hit;
}

// Merge, BEAT: borrower s's next half-row of borrowed gradients (both axes), from its block, into its ring. Meow.
static void merge_load_beat(const FusedOp& op, const pinrec::SlotBeat block_beat[2],
                            float ring_BRAM[MAX_BORROWERS][2][pinrec::BANKS][RING_ROWS], int beat_count_REG[MAX_BORROWERS]) {
#pragma HLS INLINE
    const int s = op.borrower;
    const int half = beat_count_REG[s] & 1, row = (beat_count_REG[s] >> 1) & (RING_ROWS - 1);
    for (int axis = 0; axis < 2; axis++)
        for (int j = 0; j < pinrec::LANES; j++) ring_BRAM[s][axis][half * pinrec::LANES + j][row] = block_beat[axis].v[j];
    beat_count_REG[s]++;
}

// Merge, GROUP: each addressed bank reads its next row; each entry's value lands in borrower s's addend
// lane, marked held for the chain. Meow.
static void merge_group(const FusedOp& op, const float ring_BRAM[MAX_BORROWERS][2][pinrec::BANKS][RING_ROWS],
                        int next_row_REG[MAX_BORROWERS][pinrec::BANKS],
                        float addend_REG[MAX_BORROWERS][2][pinrec::LANES], bool held_REG[MAX_BORROWERS][pinrec::LANES]) {
#pragma HLS INLINE
    const int s = op.borrower;
    float bank_val[2][pinrec::BANKS];
#pragma HLS ARRAY_PARTITION variable=bank_val complete dim=0
    for (int b = 0; b < pinrec::BANKS; b++) {
        bank_val[0][b] = 0.0f;
        bank_val[1][b] = 0.0f;
        if (entry_for_bank(op, b) >= 0) {
            const int row = next_row_REG[s][b] & (RING_ROWS - 1);
            bank_val[0][b] = ring_BRAM[s][0][b][row];
            bank_val[1][b] = ring_BRAM[s][1][b][row];
            next_row_REG[s][b]++;
        }
    }
    // Lane steering: the owner lane each entry names takes its bank's value (the 32 -> 4 gather). Meow.
    for (int e = 0; e < pinrec::FUSED_GROUP_ENTRIES; e++)
        if (op.valid[e]) {
            addend_REG[s][0][op.lane[e]] = bank_val[0][op.bank[e]];
            addend_REG[s][1][op.lane[e]] = bank_val[1][op.bank[e]];
            held_REG[s][op.lane[e]] = true;
        }
}

// Merge, at the BOUNDARY: wirelength + every holding borrower's gradient, borrowers ascending -- B3's and
// M2's order. Selected, not "+ 0.0f" for a borrower that holds nothing: -0.0 + 0.0 is +0.0. Meow.
static float merge_chain(float wl, const float addend[MAX_BORROWERS], const bool held[MAX_BORROWERS]) {
#pragma HLS INLINE
    float sum = wl;
    for (int s = 0; s < MAX_BORROWERS; s++) sum = held[s] ? sum + addend[s] : sum;
    return sum;
}

// Send, GROUP: each entry's new position (owner lane) into its bank's next row of borrower s's ring. Meow.
static void send_group(const FusedOp& op, const float new_pos[2][pinrec::LANES],
                       float ring_BRAM[MAX_BORROWERS][2][pinrec::BANKS][RING_ROWS], int next_row_REG[MAX_BORROWERS][pinrec::BANKS]) {
#pragma HLS INLINE
    const int s = op.borrower;
    for (int b = 0; b < pinrec::BANKS; b++) {
        const int e = entry_for_bank(op, b);
        if (e >= 0) {
            const int row = next_row_REG[s][b] & (RING_ROWS - 1);
            ring_BRAM[s][0][b][row] = new_pos[0][op.lane[e]];
            ring_BRAM[s][1][b][row] = new_pos[1][op.lane[e]];
            next_row_REG[s][b]++;
        }
    }
}

// Send, BEAT: borrower s's next half-row (both axes) out of its ring, for its block. Meow.
static void send_drain_beat(const FusedOp& op, const float ring_BRAM[MAX_BORROWERS][2][pinrec::BANKS][RING_ROWS],
                            int beat_count_REG[MAX_BORROWERS], pinrec::SlotBeat block_beat[2]) {
#pragma HLS INLINE
    const int s = op.borrower;
    const int half = beat_count_REG[s] & 1, row = (beat_count_REG[s] >> 1) & (RING_ROWS - 1);
    for (int axis = 0; axis < 2; axis++)
        for (int j = 0; j < pinrec::LANES; j++) block_beat[axis].v[j] = ring_BRAM[s][axis][half * pinrec::LANES + j][row];
    beat_count_REG[s]++;
}

} // namespace plalgo

#endif // PL_ITERATION_UPDATE_BORROWED_EXCHANGE_HPP
