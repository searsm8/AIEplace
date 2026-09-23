#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "modules/hpwl_computer.hpp"
#include <vector>
#include <random>
#include <algorithm>
#include <cstdio>

using namespace plalgo;

// Degree-grouped, beat-packed layout: per_degree[i] real nets of degree (MIN_NET_DEGREE+i),
// packed nets_per_beat(degree) = LANES/degree to a beat, zero-padded on each group's last beat.
// net_count/beat_count are CUMULATIVE (nets / beats with degree <= i+MIN_NET_DEGREE), matching
// resolve_beat's interface -- per_degree/beats_per_degree only exist locally, for the golden walk.
struct BeatDesign {
    int num_beats = 0;
    std::vector<int> net_count;   // [NET_DEGREES_PROCESSED], cumulative nets
    std::vector<int> beat_count;  // [NET_DEGREES_PROCESSED], cumulative beats
    std::vector<int> per_degree;  // [NET_DEGREES_PROCESSED], real net count per degree, golden only
};

static BeatDesign build_design(unsigned seed) {
    BeatDesign d;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> count_pick(0, 40);

    d.per_degree.assign(NET_DEGREES_PROCESSED, 0);
    for (int i = 0; i < NET_DEGREES_PROCESSED; i++) d.per_degree[i] = count_pick(rng);
    d.per_degree[3] = 0;  // degree 5 deliberately empty -- exercises the boundary-skip on a tie

    d.net_count.assign(NET_DEGREES_PROCESSED, 0);
    d.beat_count.assign(NET_DEGREES_PROCESSED, 0);
    int nets_running = 0, beats_running = 0;
    for (int i = 0; i < NET_DEGREES_PROCESSED; i++) {
        const int degree        = MIN_NET_DEGREE + i;
        const int nets_per_beat = LANES / degree;
        const int group_beats   = (d.per_degree[i] + nets_per_beat - 1) / nets_per_beat; // ceil, 0 if per_degree[i]==0
        nets_running  += d.per_degree[i];
        beats_running += group_beats;
        d.net_count[i]  = nets_running;
        d.beat_count[i] = beats_running;
    }
    d.num_beats = beats_running;
    return d;
}

// Golden: independent walk of the same degree-grouped layout, computed straight from per_degree
// rather than from resolve_beat's cumulative-count scan.
static void golden(const BeatDesign& d, std::vector<BeatInfo>& info) {
    info.clear();
    int net_offset = 0;
    for (int i = 0; i < NET_DEGREES_PROCESSED; i++) {
        const int degree        = MIN_NET_DEGREE + i;
        const int nets_per_beat = LANES / degree;
        int remaining = d.per_degree[i];
        while (remaining > 0) {
            BeatInfo bi;
            bi.degree        = degree;
            bi.nets_per_beat = nets_per_beat;
            bi.net_offset    = net_offset;
            bi.real_net_count = (nets_per_beat < remaining) ? nets_per_beat : remaining;
            info.push_back(bi);
            net_offset += bi.real_net_count;
            remaining  -= bi.real_net_count;
        }
    }
}

// Packs actual pin coordinates into the same degree-grouped, beat-packed layout `d` describes,
// and independently computes each real net's HPWL span straight from its own (unpacked) pins --
// this is the golden for hpwl_computer() itself, not just its beat bookkeeping.
struct PackedDesign {
    std::vector<InBeat>  pin_beats;
    std::vector<float> golden_hpwl;
};

static PackedDesign pack_design(const BeatDesign& d, unsigned seed) {
    PackedDesign p;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(0.0f, 10000.0f);

    p.pin_beats.resize(d.num_beats);
    p.golden_hpwl.resize(d.net_count.back());

    int beat_idx = 0, net_idx = 0;
    for (int i = 0; i < NET_DEGREES_PROCESSED; i++) {
        const int degree        = MIN_NET_DEGREE + i;
        const int nets_per_beat = LANES / degree;
        int remaining = d.per_degree[i];
        while (remaining > 0) {
            const int real_this_beat = (nets_per_beat < remaining) ? nets_per_beat : remaining;
            InBeat blk;
            for (int k = 0; k < LANES; k++) blk.v[k] = 0.0f;
            for (int seg = 0; seg < real_this_beat; seg++) {
                float golden_max = -1e30f, golden_min = 1e30f;
                for (int k = 0; k < degree; k++) {
                    const float x = pos(rng);
                    blk.v[seg * degree + k] = x;
                    golden_max = std::max(golden_max, x);
                    golden_min = std::min(golden_min, x);
                }
                p.golden_hpwl[net_idx] = golden_max - golden_min;
                net_idx++;
            }
            p.pin_beats[beat_idx] = blk;
            beat_idx++;
            remaining -= real_this_beat;
        }
    }
    return p;
}

int main() {
    BeatDesign d = build_design(20260921u);

    std::vector<BeatInfo> golden_info;
    golden(d, golden_info);

    printf("[info] %d beats, per_degree = [", d.num_beats);
    for (int i = 0; i < NET_DEGREES_PROCESSED; i++) printf("%d%s", d.per_degree[i], i + 1 < NET_DEGREES_PROCESSED ? "," : "");
    printf("]\n");

    int bad = 0;
    for (int b = 0; b < d.num_beats; b++) {
        BeatInfo got = resolve_beat(b, d.net_count.data(), d.beat_count.data());
        const BeatInfo& want = golden_info[b];
        if (got.degree != want.degree || got.nets_per_beat != want.nets_per_beat ||
            got.net_offset != want.net_offset || got.real_net_count != want.real_net_count) {
            if (bad < 5) {
                printf("FAIL beat %d: got {degree=%d nets_per_beat=%d net_offset=%d real_net_count=%d} "
                       "golden {degree=%d nets_per_beat=%d net_offset=%d real_net_count=%d}\n",
                       b, got.degree, got.nets_per_beat, got.net_offset, got.real_net_count,
                       want.degree, want.nets_per_beat, want.net_offset, want.real_net_count);
            }
            bad++;
        }
    }
    bool tree_ok = true;
    std::mt19937 rng(20260922u);
    std::uniform_real_distribution<float> pos(0.0f, 10000.0f);
    for (int trial = 0; trial < 50; trial++) {
        float v[LANES];
        for (int k = 0; k < LANES; k++) v[k] = pos(rng);

        TreeOutputs t = build_tree_outputs(v);

        for (int degree = MIN_NET_DEGREE; degree <= LANES; degree++) {
            const int j              = degree - MIN_NET_DEGREE;
            const int nets_per_beat  = LANES / degree;

            float lane_hpwl[LANES];
            select_lane_hpwl(t, degree, lane_hpwl);

            for (int seg = 0; seg < nets_per_beat; seg++) {
                float golden_max = -1e30f, golden_min = 1e30f;
                for (int k = 0; k < degree; k++) {
                    const float x = v[seg * degree + k];
                    golden_max = std::max(golden_max, x);
                    golden_min = std::min(golden_min, x);
                }
                if (t.max_deg[j][seg] != golden_max || t.min_deg[j][seg] != golden_min) {
                    printf("FAIL tree degree %d seg %d: got max=%.6f min=%.6f golden max=%.6f min=%.6f\n",
                           degree, seg, t.max_deg[j][seg], t.min_deg[j][seg], golden_max, golden_min);
                    tree_ok = false;
                }
                for (int k = 0; k < degree; k++) {
                    const int lane = seg * degree + k;
                    if (lane_hpwl[lane] != golden_max - golden_min) {
                        printf("FAIL selector degree %d lane %d: got %.6f golden %.6f\n",
                               degree, lane, lane_hpwl[lane], golden_max - golden_min);
                        tree_ok = false;
                    }
                }
            }
        }
    }
    printf("[info] tree+selector checked over 50 random beats x 15 degrees x all segments/lanes\n");

    PackedDesign packed = pack_design(d, 20260922u);
    std::vector<OutBeat> out_beats(d.num_beats);
    hpwl_computer(d.net_count.data(), d.beat_count.data(), packed.pin_beats.data(), out_beats.data(), d.num_beats);

    int hpwl_bad = 0;
    for (int b = 0; b < d.num_beats; b++) {
        const BeatInfo& info = golden_info[b];
        for (int net = 0; net < info.real_net_count; net++) {
            const float got    = out_beats[b].v[net];
            const float wanted = packed.golden_hpwl[info.net_offset + net];
            if (got != wanted) {
                if (hpwl_bad < 5) printf("FAIL beat %d net %d: got %.6f golden %.6f\n", b, net, got, wanted);
                hpwl_bad++;
            }
        }
    }
    if (hpwl_bad) printf("FAIL: %d nets mismatched\n", hpwl_bad);
    printf("[info] hpwl_computer checked end-to-end over %zu real nets, packed into %d output beats\n",
           packed.golden_hpwl.size(), d.num_beats);

    const bool ok = (bad == 0) && (d.num_beats == (int)golden_info.size()) && tree_ok && (hpwl_bad == 0);
    if (bad) printf("FAIL: %d/%d beats mismatched\n", bad, d.num_beats);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
