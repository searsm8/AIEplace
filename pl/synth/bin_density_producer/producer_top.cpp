// producer_top.cpp -- #48 round 3 costing: ONE producer of the bank-FIFO deposit design, synthesized on
// its own to price P copies of it (PLAN_48_bin_density_lanes_20261020.md, round 3).
//
// Per cycle it takes one slot (x, y, footprint index) and emits the cell's up to 4 x 4 updates for the
// 64-bank grid (bank = 8*(col%8) + row%8, address (col/8, row/8)): bank, address, fixed-point increment.
// The arithmetic is bin_density_CU's (footprint_window, overlap_x/y, to_fixed). The bin index is
// PRODUCER_DIVIDE=1: (int)(xl / bin_w), today's golden; PRODUCER_DIVIDE=0: (int)(xl * inv_bin_w), the
// proposed golden (not yet the model's: costing only). Streams in and out, so the cost is the
// producer's alone, not an m_axi port's. Meow.

#include "format/density_footprint.hpp"

#include <hls_stream.h>

#ifndef PRODUCER_DIVIDE
#define PRODUCER_DIVIDE 0
#endif

constexpr int SIDE = densrec::WINDOW_BINS;          // 4: window bins per axis
constexpr int UPDATES = SIDE * SIDE;                // 16 updates per cell at most
constexpr int BANK_SIDE = 8;                        // 8 x 8 interleave: 64 banks

struct SlotIn { float x, y; uint32_t footprint; };
struct Updates {
    uint32_t addr[UPDATES];   // word within the bank: (col/8) * (grid_bins/8) + row/8
    uint32_t inc[UPDATES];    // to_fixed(ow * oh * weight')
    uint8_t  bank[UPDATES];   // 8*(col%8) + row%8
    bool     valid[UPDATES];
};

static inline int bin_index(float v, float bin, float inv_bin) {
#pragma HLS INLINE
#if PRODUCER_DIVIDE
    return (int)(v / bin);
#else
    return (int)(v * inv_bin);
#endif
}

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

    // One cell per cycle: window corners, 4 column and 4 row overlaps, 16 products, 16 roundings. Meow.
produce:
    for (int s = 0; s < num_slots; s++) {
#pragma HLS PIPELINE II=1
        const SlotIn in = slots.read();
        const densrec::Footprint fp = table_BRAM[in.footprint];
        const float xl = in.x + fp.half_w - fp.half_cw, yl = in.y + fp.half_h - fp.half_ch;
        const float xh = xl + 2.0f * fp.half_cw, yh = yl + 2.0f * fp.half_ch;
        int col_lo = bin_index(xl, bin_w, inv_bin_w);  if (col_lo < 0) col_lo = 0;
        int col_hi = bin_index(xh, bin_w, inv_bin_w);  if (col_hi > grid_bins - 1) col_hi = grid_bins - 1;
        int row_lo = bin_index(yl, bin_h, inv_bin_h);  if (row_lo < 0) row_lo = 0;
        int row_hi = bin_index(yh, bin_h, inv_bin_h);  if (row_hi > grid_bins - 1) row_hi = grid_bins - 1;

        float ow[SIDE], oh[SIDE];
#pragma HLS ARRAY_PARTITION variable=ow complete
#pragma HLS ARRAY_PARTITION variable=oh complete
        for (int k = 0; k < SIDE; k++) {
            const float bin_xl = (col_lo + k) * bin_w, bin_yl = (row_lo + k) * bin_h;
            const float wx = (xh < bin_xl + bin_w ? xh : bin_xl + bin_w) - (xl > bin_xl ? xl : bin_xl);
            const float wy = (yh < bin_yl + bin_h ? yh : bin_yl + bin_h) - (yl > bin_yl ? yl : bin_yl);
            ow[k] = col_lo + k <= col_hi && wx > 0.0f ? wx : 0.0f;
            oh[k] = row_lo + k <= row_hi && wy > 0.0f ? wy : 0.0f;
        }

        Updates out;
#pragma HLS ARRAY_PARTITION variable=out.addr complete
#pragma HLS ARRAY_PARTITION variable=out.inc complete
#pragma HLS ARRAY_PARTITION variable=out.bank complete
#pragma HLS ARRAY_PARTITION variable=out.valid complete
        // Window position (dc, dr): its bin, bank and address; the golden's product order (ow*oh)*w. Meow.
        for (int dc = 0; dc < SIDE; dc++)
            for (int dr = 0; dr < SIDE; dr++) {
                const int u = dc * SIDE + dr, col = col_lo + dc, row = row_lo + dr;
                out.valid[u] = ow[dc] > 0.0f && oh[dr] > 0.0f;
                out.bank[u]  = (uint8_t)(BANK_SIDE * (col % BANK_SIDE) + row % BANK_SIDE);
                out.addr[u]  = (uint32_t)((col / BANK_SIDE) * (grid_bins / BANK_SIDE) + row / BANK_SIDE);
                out.inc[u]   = densrec::to_fixed(ow[dc] * oh[dr] * fp.weight);
            }
        updates.write(out);
    }
}
