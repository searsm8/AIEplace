// banked_top.cpp -- #48 round 3 costing: the whole bank-FIFO deposit (Mark), synthesized to price it.
//
// 8 producers (../bin_density_producer/producer.hpp) each turn one cell per cycle into up to 16
// fixed-point amounts. The grid is 64 URAM banks, bank = 8*(col%8) + row%8, address (col/8, row/8): any
// <= 4 x 4 window hits 16 different banks. Every producer has its OWN small FIFO into every bank
// (8 x 64 = 512), so each FIFO has one writer and one reader and nothing arbitrates; a producer stalls
// only when one of its own FIFOs is full. Each bank, every cycle, takes one update from the first of its 8
// FIFOs that has one and does its read-add-write, forwarding its last BANKED_FORWARD_DEPTH writes (the
// URAM write lands several stages after the read; see bin_density_CU.hpp). Integer adds make the order in
// which updates reach a bank irrelevant, so the result equals the plain deposit bit for bit.
//
// Costing scope: the deposit only. No fixed base load, no overflow reduce, no slow path; the result is
// written out raw (one bank word per cycle), not as rho beats. Grid GRID_MAX (1024): 64 banks x 16K
// words x 36 b = 128 URAMs. Meow.

#include "../bin_density_producer/producer.hpp"

#include <hls_stream.h>

#ifdef __SYNTHESIS__
#include <ap_int.h>
typedef ap_uint<densrec::URAM_WORD_WIDTH> acc_word;
#else
typedef uint64_t acc_word;   // wider off-synthesis, as bin_density_CU
#endif

#ifndef BANKED_FORWARD_DEPTH
#define BANKED_FORWARD_DEPTH 6
#endif

constexpr int PRODUCERS     = 8;
constexpr int BANK_SIDE     = 8;                                      // 8 x 8 interleave
constexpr int NUM_BANKS     = BANK_SIDE * BANK_SIDE;                  // 64
constexpr int BANK_WORDS    = (densrec::GRID_MAX / BANK_SIDE) * (densrec::GRID_MAX / BANK_SIDE);   // 16K at 1024
constexpr int FIFO_DEPTH    = 8;
constexpr int FORWARD_DEPTH = BANKED_FORWARD_DEPTH;
constexpr int TABLE_MAX     = 128;                                    // measured 4..105 on the 44 designs

struct BankUpdate { uint16_t addr; uint32_t inc; bool last; };   // last: the producer is done

// One producer: its footprint table, then one cell per cycle; each bank gets the window position it owns.
// The bank -> window position step (dc, dr) is the fill network: a 16:1 select per bank. Meow.
static void producer_process(const densrec::Footprint* footprints_DDR, int num_footprints, const SlotIn* slots_DDR,
                             int num_slots, int grid_bins, float bin_w, float bin_h, float fixed_scale,
                             hls::stream<BankUpdate> to_bank[NUM_BANKS]) {
    densrec::Footprint table_BRAM[TABLE_MAX];
load_table:
    for (int i = 0; i < num_footprints; i++) {
#pragma HLS LOOP_TRIPCOUNT max=TABLE_MAX
        densrec::Footprint fp = footprints_DDR[i];
        fp.weight *= fixed_scale;
        table_BRAM[i] = fp;
    }
produce:
    for (int s = 0; s < num_slots; s++) {
#pragma HLS PIPELINE II=1
        const SlotIn in = slots_DDR[s];
        const densrec::Footprint fp = table_BRAM[in.footprint == densrec::NO_DEPOSIT ? 0 : in.footprint];
        const CellAmounts a = produce_cell(in, fp, grid_bins, bin_w, bin_h, 1.0f / bin_w, 1.0f / bin_h);
    fill:
        for (int b = 0; b < NUM_BANKS; b++) {
#pragma HLS UNROLL
            const int dc = (b / BANK_SIDE - a.col_lo) & (BANK_SIDE - 1);   // which window column bank b owns
            const int dr = (b % BANK_SIDE - a.row_lo) & (BANK_SIDE - 1);
            if (dc < PRODUCER_SIDE && dr < PRODUCER_SIDE && a.valid[dc * PRODUCER_SIDE + dr]) {
                const int col = a.col_lo + dc, row = a.row_lo + dr;
                to_bank[b].write(BankUpdate{(uint16_t)((col / BANK_SIDE) * (grid_bins / BANK_SIDE) + row / BANK_SIDE),
                                            a.inc[dc * PRODUCER_SIDE + dr], false});
            }
        }
    }
done:
    for (int b = 0; b < NUM_BANKS; b++) {
#pragma HLS UNROLL
        to_bank[b].write(BankUpdate{0, 0, true});
    }
}

// The 64 banks: each takes one update per cycle and adds it in, until all 8 producers have said `last`. Meow.
static void drain_banks(hls::stream<BankUpdate> from[PRODUCERS][NUM_BANKS], acc_word rho_URAM[NUM_BANKS][BANK_WORDS]) {
    int finished[NUM_BANKS];
    uint16_t fwd_addr[NUM_BANKS][FORWARD_DEPTH + 1];
    acc_word fwd_value[NUM_BANKS][FORWARD_DEPTH + 1];
    bool     fwd_valid[NUM_BANKS][FORWARD_DEPTH + 1];
#pragma HLS ARRAY_PARTITION variable=finished complete
#pragma HLS ARRAY_PARTITION variable=fwd_addr complete dim=0
#pragma HLS ARRAY_PARTITION variable=fwd_value complete dim=0
#pragma HLS ARRAY_PARTITION variable=fwd_valid complete dim=0
    for (int b = 0; b < NUM_BANKS; b++) {
#pragma HLS UNROLL
        finished[b] = 0;
        for (int d = 0; d <= FORWARD_DEPTH; d++) fwd_valid[b][d] = false;
    }
    bool done = false;
drain:
    while (!done) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=rho_URAM inter false   // safe ONLY because of the forwarding registers
        bool all_finished = true;
    banks:
        for (int b = 0; b < NUM_BANKS; b++) {
#pragma HLS UNROLL
            // Fixed priority over this bank's 8 FIFOs: all 8 "has data" flags at once, then one read, so
            // the pick is a priority encoder rather than a chain of 8 reads in one cycle. Meow.
            int pick = -1;
            for (int p = PRODUCERS - 1; p >= 0; p--)
                if (!from[p][b].empty()) pick = p;
            BankUpdate u{0, 0, false};
            const bool got = pick >= 0;
            for (int p = 0; p < PRODUCERS; p++)
                if (pick == p) u = from[p][b].read();
            const bool add = got && !u.last;
            if (got && u.last) finished[b]++;
            acc_word current = rho_URAM[b][u.addr];
            for (int d = FORWARD_DEPTH - 1; d >= 0; d--)
                if (fwd_valid[b][d] && fwd_addr[b][d] == u.addr) current = fwd_value[b][d];
            const acc_word updated = current + u.inc;
            if (add) rho_URAM[b][u.addr] = updated;
            for (int d = FORWARD_DEPTH; d > 0; d--) {
                fwd_addr[b][d] = fwd_addr[b][d - 1]; fwd_value[b][d] = fwd_value[b][d - 1]; fwd_valid[b][d] = fwd_valid[b][d - 1];
            }
            fwd_addr[b][0] = u.addr; fwd_value[b][0] = updated; fwd_valid[b][0] = add;
            all_finished &= finished[b] == PRODUCERS;
        }
        done = all_finished;
    }
}

#define PRODUCER_ARGS(p) const densrec::Footprint* footprints##p, const SlotIn* slots##p, int num_slots##p
#define PRODUCER_CALL(p) producer_process(footprints##p, num_footprints, slots##p, num_slots##p, grid_bins, bin_w, \
                                          bin_h, fixed_scale, fifos[p]);

static void deposit_region(PRODUCER_ARGS(0), PRODUCER_ARGS(1), PRODUCER_ARGS(2), PRODUCER_ARGS(3),
                           PRODUCER_ARGS(4), PRODUCER_ARGS(5), PRODUCER_ARGS(6), PRODUCER_ARGS(7),
                           int num_footprints, int grid_bins, float bin_w, float bin_h, float fixed_scale,
                           acc_word rho_URAM[NUM_BANKS][BANK_WORDS]) {
#pragma HLS DATAFLOW
    hls::stream<BankUpdate> fifos[PRODUCERS][NUM_BANKS];
#pragma HLS STREAM variable=fifos depth=FIFO_DEPTH
    PRODUCER_CALL(0) PRODUCER_CALL(1) PRODUCER_CALL(2) PRODUCER_CALL(3)
    PRODUCER_CALL(4) PRODUCER_CALL(5) PRODUCER_CALL(6) PRODUCER_CALL(7)
    drain_banks(fifos, rho_URAM);
}

// Each producer gets its own slice of the cells (and its own copy of the footprint table) on its own
// m_axi port. The result goes out raw: word a of bank b at rho_out[a * 64 + b]. Meow.
extern "C" void banked_top(PRODUCER_ARGS(0), PRODUCER_ARGS(1), PRODUCER_ARGS(2), PRODUCER_ARGS(3),
                           PRODUCER_ARGS(4), PRODUCER_ARGS(5), PRODUCER_ARGS(6), PRODUCER_ARGS(7),
                           int num_footprints, int grid_bins, float bin_w, float bin_h, float fixed_scale,
                           unsigned long long* rho_out) {
#pragma HLS INTERFACE m_axi port=footprints0 bundle=gmem0 offset=slave depth=128
#pragma HLS INTERFACE m_axi port=slots0 bundle=gmem0 offset=slave depth=65536
#pragma HLS INTERFACE m_axi port=footprints1 bundle=gmem1 offset=slave depth=128
#pragma HLS INTERFACE m_axi port=slots1 bundle=gmem1 offset=slave depth=65536
#pragma HLS INTERFACE m_axi port=footprints2 bundle=gmem2 offset=slave depth=128
#pragma HLS INTERFACE m_axi port=slots2 bundle=gmem2 offset=slave depth=65536
#pragma HLS INTERFACE m_axi port=footprints3 bundle=gmem3 offset=slave depth=128
#pragma HLS INTERFACE m_axi port=slots3 bundle=gmem3 offset=slave depth=65536
#pragma HLS INTERFACE m_axi port=footprints4 bundle=gmem4 offset=slave depth=128
#pragma HLS INTERFACE m_axi port=slots4 bundle=gmem4 offset=slave depth=65536
#pragma HLS INTERFACE m_axi port=footprints5 bundle=gmem5 offset=slave depth=128
#pragma HLS INTERFACE m_axi port=slots5 bundle=gmem5 offset=slave depth=65536
#pragma HLS INTERFACE m_axi port=footprints6 bundle=gmem6 offset=slave depth=128
#pragma HLS INTERFACE m_axi port=slots6 bundle=gmem6 offset=slave depth=65536
#pragma HLS INTERFACE m_axi port=footprints7 bundle=gmem7 offset=slave depth=128
#pragma HLS INTERFACE m_axi port=slots7 bundle=gmem7 offset=slave depth=65536
#pragma HLS INTERFACE m_axi port=rho_out bundle=gmem8 offset=slave depth=1048576
#ifdef __SYNTHESIS__
    acc_word rho_URAM[NUM_BANKS][BANK_WORDS];
#else
    static acc_word rho_URAM[NUM_BANKS][BANK_WORDS];
#endif
#pragma HLS ARRAY_PARTITION variable=rho_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=rho_URAM type=ram_2p impl=uram latency=3   // registered read: the RMW spans more stages, forwarding covers them
clear:
    for (int a = 0; a < BANK_WORDS; a++) {
#pragma HLS PIPELINE II=1
        for (int b = 0; b < NUM_BANKS; b++) rho_URAM[b][a] = 0;
    }
    deposit_region(footprints0, slots0, num_slots0, footprints1, slots1, num_slots1, footprints2, slots2, num_slots2,
                   footprints3, slots3, num_slots3, footprints4, slots4, num_slots4, footprints5, slots5, num_slots5,
                   footprints6, slots6, num_slots6, footprints7, slots7, num_slots7, num_footprints, grid_bins, bin_w,
                   bin_h, fixed_scale, rho_URAM);
write_out:
    for (int i = 0; i < BANK_WORDS * NUM_BANKS; i++) {
#pragma HLS PIPELINE II=1
        rho_out[i] = (unsigned long long)rho_URAM[i % NUM_BANKS][i / NUM_BANKS];
    }
}
