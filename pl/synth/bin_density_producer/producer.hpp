#ifndef PL_SYNTH_BIN_DENSITY_PRODUCER_HPP
#define PL_SYNTH_BIN_DENSITY_PRODUCER_HPP

// producer.hpp -- one bin-density producer's arithmetic (#48 round 3), shared by producer_top.cpp (cost
// of one producer) and ../bin_density_banked (the whole banked deposit). One cell in, its window's up to
// 4 x 4 fixed-point amounts out, in window order (dc, dr). Same arithmetic as bin_density_CU
// (footprint_window, overlap_x/y, to_fixed). PRODUCER_DIVIDE=0 (default) indexes bins by multiply, as the
// golden does since 2026-10-27; 1 by divide, for costing only: Vivado put the 4 dividers at +7.8K LUT (#48). Meow.

#include "format/density_footprint.hpp"

#ifndef PRODUCER_DIVIDE
#define PRODUCER_DIVIDE 0
#endif

constexpr int PRODUCER_SIDE = densrec::WINDOW_BINS;                 // 4: window bins per axis
constexpr int PRODUCER_UPDATES = PRODUCER_SIDE * PRODUCER_SIDE;     // 16

// footprint == NO_DEPOSIT: nothing to do. Padded to 16 bytes so one cell is one bus read (12 bytes took
// two and halved a producer's rate). Meow.
struct SlotIn { float x, y; uint32_t footprint, pad; };

struct CellAmounts {
    int      col_lo, row_lo;               // the window's first bin
    uint32_t inc[PRODUCER_UPDATES];        // window position dc * 4 + dr
    bool     valid[PRODUCER_UPDATES];
};

static inline int producer_bin_index(float v, float bin, float inv_bin) {
#pragma HLS INLINE
#if PRODUCER_DIVIDE
    (void)inv_bin;
    return (int)(v / bin);
#else
    (void)bin;
    return (int)(v * inv_bin);
#endif
}

// fp.weight is already scaled by 2^F / bin_area (the host's table, as bin_density_CU loads it). Meow.
static inline CellAmounts produce_cell(const SlotIn& in, const densrec::Footprint& fp, int grid_bins, float bin_w,
                                       float bin_h, float inv_bin_w, float inv_bin_h) {
#pragma HLS INLINE
    const bool live = in.footprint != densrec::NO_DEPOSIT;
    const float xl = in.x + fp.half_w - fp.half_cw, yl = in.y + fp.half_h - fp.half_ch;
    const float xh = xl + 2.0f * fp.half_cw, yh = yl + 2.0f * fp.half_ch;
    int col_lo = producer_bin_index(xl, bin_w, inv_bin_w);  if (col_lo < 0) col_lo = 0;
    int col_hi = producer_bin_index(xh, bin_w, inv_bin_w);  if (col_hi > grid_bins - 1) col_hi = grid_bins - 1;
    int row_lo = producer_bin_index(yl, bin_h, inv_bin_h);  if (row_lo < 0) row_lo = 0;
    int row_hi = producer_bin_index(yh, bin_h, inv_bin_h);  if (row_hi > grid_bins - 1) row_hi = grid_bins - 1;

    float ow[PRODUCER_SIDE], oh[PRODUCER_SIDE];
#pragma HLS ARRAY_PARTITION variable=ow complete
#pragma HLS ARRAY_PARTITION variable=oh complete
    for (int k = 0; k < PRODUCER_SIDE; k++) {
        const float bin_xl = (col_lo + k) * bin_w, bin_yl = (row_lo + k) * bin_h;
        const float wx = (xh < bin_xl + bin_w ? xh : bin_xl + bin_w) - (xl > bin_xl ? xl : bin_xl);
        const float wy = (yh < bin_yl + bin_h ? yh : bin_yl + bin_h) - (yl > bin_yl ? yl : bin_yl);
        ow[k] = col_lo + k <= col_hi && wx > 0.0f ? wx : 0.0f;
        oh[k] = row_lo + k <= row_hi && wy > 0.0f ? wy : 0.0f;
    }

    CellAmounts out;
    out.col_lo = col_lo;
    out.row_lo = row_lo;
    // The golden's product order, (ow * oh) * weight: 32 float multiplies, where the DSPs go. Meow.
    for (int dc = 0; dc < PRODUCER_SIDE; dc++)
        for (int dr = 0; dr < PRODUCER_SIDE; dr++) {
            const int u = dc * PRODUCER_SIDE + dr;
            out.valid[u] = live && ow[dc] > 0.0f && oh[dr] > 0.0f;
            out.inc[u] = densrec::to_fixed(ow[dc] * oh[dr] * fp.weight);
        }
    return out;
}

#endif // PL_SYNTH_BIN_DENSITY_PRODUCER_HPP
