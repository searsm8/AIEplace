// hpwl_computer_sw.cpp -- plain C++ model of bring_up/hpwl_computer (no HLS headers, no pragmas).
//
// Same I/O contract as plalgo::hpwl_computer(): nets grouped by degree (2..16), packed LANES/degree
// nets to a 16-lane beat, zero-padded on each group's last beat; net_count/beat_count are the
// CUMULATIVE nets/beats through each degree. Output beat b holds the HPWL span of its real nets in
// slots 0..real_net_count-1 and 0.0f in the rest. Where the hardware resolves each beat's degree
// independently and reduces through Dhar's shared comparator tree, this walks the degree groups in
// order and takes each net's max-min directly; max/min are exact in float, so results are bit-exact.
//
// Build + run standalone:
//   g++ -O2 -std=c++17 hpwl_computer_sw.cpp -o hpwl_computer_sw && ./hpwl_computer_sw
// Also cross-check bit-exact against the HLS module source:
//   g++ -O2 -std=c++17 -DCHECK_HLS -I../../../test -I../src hpwl_computer_sw.cpp -o hpwl_computer_sw
// Meow.

#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

namespace sw {

constexpr int LANES                 = 16;
constexpr int MIN_NET_DEGREE        = 2;
constexpr int NET_DEGREES_PROCESSED = LANES - MIN_NET_DEGREE + 1;
constexpr int MAX_NETS_PER_BEAT     = LANES / MIN_NET_DEGREE;

struct InBeat  { float v[LANES]; };
struct OutBeat { float v[MAX_NETS_PER_BEAT]; };

void hpwl_computer(const int* net_count, const int* beat_count,
                   const InBeat* pin_beats, OutBeat* out_beats, int num_beats) {
    int group_start_net = 0, group_start_beat = 0;
    for (int degree_idx = 0; degree_idx < NET_DEGREES_PROCESSED; degree_idx++) {
        const int degree        = MIN_NET_DEGREE + degree_idx;
        const int nets_per_beat = LANES / degree;
        int remaining_nets      = net_count[degree_idx] - group_start_net;

        for (int beat = group_start_beat; beat < beat_count[degree_idx] && beat < num_beats; beat++) {
            OutBeat out_beat = {};
            const int real_net_count = std::min(nets_per_beat, remaining_nets);
            for (int net = 0; net < real_net_count; net++) {
                const float* pins = &pin_beats[beat].v[net * degree];
                const auto [lo, hi] = std::minmax_element(pins, pins + degree);
                out_beat.v[net] = *hi - *lo;
            }
            out_beats[beat] = out_beat;
            remaining_nets -= real_net_count;
        }
        group_start_net  = net_count[degree_idx];
        group_start_beat = beat_count[degree_idx];
    }
}

} // namespace sw

#ifdef CHECK_HLS
#include "tier1_stub.hpp"
#include "modules/hpwl_computer.hpp"
#endif

// Random degree-grouped design: per-degree net counts, cumulative tables, packed pin beats, and a
// per-net reference HPWL computed from the unpacked pins as they are generated. Meow.
struct Design {
    std::vector<int> net_count, beat_count;
    std::vector<sw::InBeat> pin_beats;
    std::vector<float> reference_hpwl;
};

static Design build_design(unsigned seed) {
    using namespace sw;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> count_pick(0, 40);
    std::uniform_real_distribution<float> pos(0.0f, 10000.0f);

    Design d;
    int nets_running = 0;
    for (int degree_idx = 0; degree_idx < NET_DEGREES_PROCESSED; degree_idx++) {
        const int degree        = MIN_NET_DEGREE + degree_idx;
        const int nets_per_beat = LANES / degree;
        // degree 5 left empty: the group-boundary walk must skip a zero-beat group. Meow.
        int remaining = (degree == 5) ? 0 : count_pick(rng);
        nets_running += remaining;
        while (remaining > 0) {
            InBeat beat = {};
            const int real_this_beat = std::min(nets_per_beat, remaining);
            for (int net = 0; net < real_this_beat; net++) {
                float hi = -1e30f, lo = 1e30f;
                for (int k = 0; k < degree; k++) {
                    const float x = pos(rng);
                    beat.v[net * degree + k] = x;
                    hi = std::max(hi, x);
                    lo = std::min(lo, x);
                }
                d.reference_hpwl.push_back(hi - lo);
            }
            d.pin_beats.push_back(beat);
            remaining -= real_this_beat;
        }
        d.net_count.push_back(nets_running);
        d.beat_count.push_back((int)d.pin_beats.size());
    }
    return d;
}

int main() {
    using namespace sw;
    const Design d = build_design(20260921u);
    const int num_beats = (int)d.pin_beats.size();

    std::vector<OutBeat> out_beats(num_beats);
    hpwl_computer(d.net_count.data(), d.beat_count.data(), d.pin_beats.data(), out_beats.data(), num_beats);

    // Reference nets are in packing order, so unpack the output beats in that same order. Meow.
    int bad = 0, net_idx = 0;
    double total_hpwl = 0.0;
    int beat = 0;
    for (int degree_idx = 0; degree_idx < NET_DEGREES_PROCESSED; degree_idx++) {
        const int nets_per_beat = LANES / (MIN_NET_DEGREE + degree_idx);
        for (; beat < d.beat_count[degree_idx]; beat++) {
            for (int slot = 0; slot < MAX_NETS_PER_BEAT; slot++) {
                const bool real = slot < nets_per_beat && net_idx < d.net_count[degree_idx];
                const float want = real ? d.reference_hpwl[net_idx++] : 0.0f;
                if (out_beats[beat].v[slot] != want) {
                    if (bad < 5) printf("FAIL beat %d slot %d: got %.6f want %.6f\n",
                                        beat, slot, out_beats[beat].v[slot], want);
                    bad++;
                }
                if (real) total_hpwl += want;
            }
        }
    }
    printf("[info] %d beats, %d nets, total HPWL %.3f\n", num_beats, net_idx, total_hpwl);

#ifdef CHECK_HLS
    std::vector<plalgo::OutBeat> hls_out(num_beats);
    plalgo::hpwl_computer(d.net_count.data(), d.beat_count.data(),
                          reinterpret_cast<const plalgo::InBeat*>(d.pin_beats.data()),
                          hls_out.data(), num_beats);
    int hls_bad = 0;
    for (int b = 0; b < num_beats; b++)
        for (int slot = 0; slot < MAX_NETS_PER_BEAT; slot++)
            if (hls_out[b].v[slot] != out_beats[b].v[slot]) {
                if (hls_bad < 5) printf("FAIL vs HLS beat %d slot %d: hls %.6f sw %.6f\n",
                                        b, slot, hls_out[b].v[slot], out_beats[b].v[slot]);
                hls_bad++;
            }
    printf("[info] bit-exact vs plalgo::hpwl_computer: %d mismatched slots\n", hls_bad);
    bad += hls_bad;
#endif

    printf("%s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
