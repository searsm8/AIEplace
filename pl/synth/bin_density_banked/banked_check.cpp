// banked_check.cpp -- banked_top.cpp in C against the plain deposit (bin_density_CU's footprint_window,
// overlap_x/y, to_fixed, one bin at a time), BIT FOR BIT, on random cells split over the 8 producers
// (with empty slots mixed in). C simulation runs the processes one after another, so this checks the
// arithmetic and the bank mapping, not the forwarding depth (that needs RTL co-sim). Exit 0 = pass. Meow.
//   g++ -std=c++14 -O2 -I../../src -I$XILINX_HLS/include banked_check.cpp banked_top.cpp

#include "modules/bin_density_CU.hpp"
#include "../bin_density_producer/producer.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#define ARGS(p) const densrec::Footprint*, const SlotIn*, int
extern "C" void banked_top(ARGS(0), ARGS(1), ARGS(2), ARGS(3), ARGS(4), ARGS(5), ARGS(6), ARGS(7), int, int, float,
                           float, float, unsigned long long*);

// One grid size. Below GRID_MAX the banks keep the GRID_MAX pitch, so the words past the grid must stay 0. Meow.
static long check_grid(int grid) {
    const int cells = 400000, producers = 8;
    const float die = 20013.7f, bin_w = die / grid, bin_h = die / grid, bin_area = bin_w * bin_h;
    const float fixed_scale = std::ldexp(1.0f, 18) / bin_area;
    std::vector<densrec::Footprint> table;
    for (float w : {3.0f, 9.0f, 17.0f, 30.0f, 41.0f}) table.push_back(densrec::make_footprint(w, 12.0f, false, bin_w, bin_h, 0.8f, true));
    for (const auto& fp : table) if (!densrec::fits_window(fp, bin_w, bin_h)) { printf("FAIL: footprint too wide\n"); return 1; }

    std::mt19937 rng(9);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::vector<std::vector<SlotIn>> slices(producers);
    std::vector<uint64_t> golden((size_t)grid * grid, 0);
    for (int i = 0; i < cells; i++) {
        SlotIn s{unit(rng) * die - 20.0f, unit(rng) * die - 20.0f, (uint32_t)(i % table.size()), 0};
        if (i % 11 == 5) s.footprint = densrec::NO_DEPOSIT;
        if (i % 7 == 0) { s.x = 5000.0f + unit(rng) * 30.0f; s.y = 9000.0f + unit(rng) * 30.0f; }   // a pile-up
        slices[i % producers].push_back(s);
        if (s.footprint == densrec::NO_DEPOSIT) continue;
        densrec::Footprint fp = table[s.footprint];
        fp.weight *= fixed_scale;
        const plalgo::Window w = plalgo::footprint_window(s.x, s.y, fp, grid, 1.0f / bin_w, 1.0f / bin_h);
        for (int col = w.col_lo; col <= w.col_hi; col++) {
            const float ow = plalgo::overlap_x(w, col, bin_w);
            if (ow <= 0.0f) continue;
            for (int row = w.row_lo; row <= w.row_hi; row++) {
                const float oh = plalgo::overlap_y(w, row, bin_h);
                if (oh > 0.0f) golden[(size_t)col * grid + row] += densrec::to_fixed(ow * oh * fp.weight);
            }
        }
    }

    std::vector<unsigned long long> raw((size_t)densrec::GRID_MAX * densrec::GRID_MAX);
    const int T = (int)table.size();
    banked_top(table.data(), slices[0].data(), (int)slices[0].size(), table.data(), slices[1].data(), (int)slices[1].size(),
               table.data(), slices[2].data(), (int)slices[2].size(), table.data(), slices[3].data(), (int)slices[3].size(),
               table.data(), slices[4].data(), (int)slices[4].size(), table.data(), slices[5].data(), (int)slices[5].size(),
               table.data(), slices[6].data(), (int)slices[6].size(), table.data(), slices[7].data(), (int)slices[7].size(),
               T, grid, bin_w, bin_h, fixed_scale, raw.data());

    long differ = 0;
    const int pitch = densrec::GRID_MAX / 8;
    for (size_t i = 0; i < raw.size(); i++) {
        const int b = (int)(i % 64), a = (int)(i / 64);
        const int col = (a / pitch) * 8 + b / 8, row = (a % pitch) * 8 + b % 8;
        const uint64_t want = col < grid && row < grid ? golden[(size_t)col * grid + row] : 0;
        differ += raw[i] != want;
    }
    printf("banked deposit (8 producers, 64 banks), grid %d: %ld of %zu words differ from the plain deposit -> %s\n", grid,
           differ, raw.size(), differ ? "FAIL" : "PASS");
    return differ;
}

int main() {
    long differ = 0;
    for (int grid : {densrec::GRID_MAX, 512, 336}) differ += check_grid(grid);
    return differ ? 1 : 0;
}
