// producer_check.cpp -- the producer (producer_top.cpp) in C: its updates, summed into a grid through the
// 64-bank mapping, must equal bin_density_CU's plain bin-by-bin deposit (footprint_window, overlap_x/y,
// to_fixed) BIT FOR BIT when PRODUCER_DIVIDE=0 (multiply, the golden's since #48 2026-10-27). With
// PRODUCER_DIVIDE=1 (divide) it prints how many bins differ, for costing only. Exit 0 = pass. Meow.
//   g++ -std=c++14 -O2 -I../../src -I$XILINX_HLS/include producer_check.cpp producer_top.cpp

#include "modules/bin_density_CU.hpp"

#include <hls_stream.h>

#ifndef PRODUCER_DIVIDE
#define PRODUCER_DIVIDE 0   // as producer.hpp
#endif

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

struct SlotIn { float x, y; uint32_t footprint, pad; };
struct Updates { uint32_t addr[16]; uint32_t inc[16]; uint8_t bank[16]; bool valid[16]; };
extern "C" void producer_top(hls::stream<SlotIn>&, hls::stream<Updates>&, const densrec::Footprint*, int, int, int,
                             float, float, float, float, float);

int main() {
    const int grid = 512, cells = 200000;
    const float die = 10007.3f, bin_w = die / grid, bin_h = die / grid, bin_area = bin_w * bin_h;
    const float fixed_scale = std::ldexp(1.0f, 18) / bin_area;
    std::vector<densrec::Footprint> table;
    for (float w : {3.0f, 9.0f, 17.0f, 30.0f, 41.0f}) table.push_back(densrec::make_footprint(w, 12.0f, false, bin_w, bin_h, 0.8f, true));
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    hls::stream<SlotIn> in;
    hls::stream<Updates> out;
    std::vector<SlotIn> slots;
    for (int i = 0; i < cells; i++) {
        SlotIn s{unit(rng) * die - 20.0f, unit(rng) * die - 20.0f, (uint32_t)(i % table.size()), 0};
        if (i % 3 == 0) { s.x = std::floor(s.x / bin_w) * bin_w; s.y = std::floor(s.y / bin_h) * bin_h; }   // on bin edges
        slots.push_back(s); in.write(s);
    }
    producer_top(in, out, table.data(), (int)table.size(), cells, grid, bin_w, bin_h, 1.0f / bin_w, 1.0f / bin_h, fixed_scale);

    std::vector<uint64_t> device(grid * grid, 0), golden(grid * grid, 0);
    for (int i = 0; i < cells; i++) {
        const Updates u = out.read();
        for (int k = 0; k < 16; k++)
            if (u.valid[k]) {
                const int col = (u.addr[k] / (grid / 8)) * 8 + u.bank[k] / 8, row = (u.addr[k] % (grid / 8)) * 8 + u.bank[k] % 8;
                device[col * grid + row] += u.inc[k];
            }
        densrec::Footprint fp = table[slots[i].footprint];
        fp.weight *= fixed_scale;
        const plalgo::Window w = plalgo::footprint_window(slots[i].x, slots[i].y, fp, grid, 1.0f / bin_w, 1.0f / bin_h);
        for (int col = w.col_lo; col <= w.col_hi; col++) {
            const float ow = plalgo::overlap_x(w, col, bin_w);
            if (ow <= 0.0f) continue;
            for (int row = w.row_lo; row <= w.row_hi; row++) {
                const float oh = plalgo::overlap_y(w, row, bin_h);
                if (oh > 0.0f) golden[col * grid + row] += densrec::to_fixed(ow * oh * fp.weight);
            }
        }
    }
    long differ = 0;
    for (int b = 0; b < grid * grid; b++) differ += device[b] != golden[b];
    printf("producer (%s): %ld of %d bins differ from bin_density_CU's deposit\n", PRODUCER_DIVIDE ? "divide" : "multiply", differ, grid * grid);
    return !PRODUCER_DIVIDE && differ ? 1 : 0;
}
