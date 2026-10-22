// producer_top.cpp -- #48 round 3 costing: ONE producer of the bank-FIFO deposit design, synthesized on
// its own to price P copies of it (PLAN_48_bin_density_lanes_20261020.md, round 3).
//
// Per cycle it takes one slot (x, y, footprint index) and emits the cell's up to 4 x 4 updates for the
// 64-bank grid (bank = 8*(col%8) + row%8, address (col/8, row/8)): bank, address, fixed-point increment.
// The arithmetic is producer.hpp's (bin_density_CU's). Streams in and out, so the cost is the
// producer's alone, not an m_axi port's. Meow.

#include "producer.hpp"

#include <hls_stream.h>

constexpr int BANK_SIDE = 8;   // 8 x 8 interleave: 64 banks

struct Updates {
    uint32_t addr[PRODUCER_UPDATES];   // word within the bank: (col/8) * (grid_bins/8) + row/8
    uint32_t inc[PRODUCER_UPDATES];    // to_fixed(ow * oh * weight')
    uint8_t  bank[PRODUCER_UPDATES];   // 8*(col%8) + row%8
    bool     valid[PRODUCER_UPDATES];
};

extern "C" void producer_top(hls::stream<SlotIn>& slots, hls::stream<Updates>& updates,
                             const densrec::Footprint* footprints, int num_footprints, int num_slots,
                             int grid_bins, float bin_w, float bin_h, float inv_bin_w, float inv_bin_h,
                             float fixed_scale) {
#pragma HLS INTERFACE axis port=slots
#pragma HLS INTERFACE axis port=updates
#pragma HLS INTERFACE m_axi port=footprints bundle=gmem0 offset=slave
    densrec::Footprint table_BRAM[densrec::FOOTPRINT_TABLE_MAX];

    // Footprint table, weight pre-scaled as bin_density_CU does. Meow.
load_table:
    for (int i = 0; i < num_footprints; i++) {
#pragma HLS LOOP_TRIPCOUNT max=128
        densrec::Footprint fp = footprints[i];
        fp.weight *= fixed_scale;
        table_BRAM[i] = fp;
    }

    // One cell per cycle (producer.hpp), then each window position's bank and address. Meow.
produce:
    for (int s = 0; s < num_slots; s++) {
#pragma HLS PIPELINE II=1
        const SlotIn in = slots.read();
        const CellAmounts a = produce_cell(in, table_BRAM[in.footprint == densrec::NO_DEPOSIT ? 0 : in.footprint], grid_bins, bin_w, bin_h, inv_bin_w, inv_bin_h);
        Updates out;
#pragma HLS ARRAY_PARTITION variable=out.addr complete
#pragma HLS ARRAY_PARTITION variable=out.inc complete
#pragma HLS ARRAY_PARTITION variable=out.bank complete
#pragma HLS ARRAY_PARTITION variable=out.valid complete
        for (int u = 0; u < PRODUCER_UPDATES; u++) {
            const int col = a.col_lo + u / PRODUCER_SIDE, row = a.row_lo + u % PRODUCER_SIDE;
            out.valid[u] = a.valid[u];
            out.bank[u]  = (uint8_t)(BANK_SIDE * (col % BANK_SIDE) + row % BANK_SIDE);
            out.addr[u]  = (uint32_t)((col / BANK_SIDE) * (grid_bins / BANK_SIDE) + row / BANK_SIDE);
            out.inc[u]   = a.inc[u];
        }
        updates.write(out);
    }
}
