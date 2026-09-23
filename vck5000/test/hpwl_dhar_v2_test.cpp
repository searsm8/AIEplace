#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "modules/hpwl_gradient_dhar_v2.hpp"
#include <vector>
#include <algorithm>
#include <random>
#include <cmath>
#include <cstdio>

using namespace plalgo;

// Degree-major layout: per_degree[i] nets of degree (MIN_NET_DEGREE+i), back to back in pin_x.
// net_count is CUMULATIVE (net_count[i] = nets with degree <= i+MIN_NET_DEGREE), matching the
// module's interface -- the per-degree counts only exist locally, to lay out pin_x and as the
// golden's independent walk.
struct AxisDesign {
    int num_nets = 0;
    std::vector<int>   net_count;  // [NET_DEGREE_COUNTS], cumulative
    std::vector<int>   per_degree; // [NET_DEGREE_COUNTS], for golden/layout only
    std::vector<float> pin_x;      // degree-major, net-major within each degree block
};

static AxisDesign build_design(unsigned seed) {
    AxisDesign d;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pos(0.0f, 10000.0f);
    std::uniform_int_distribution<int>    count_pick(50, 200);

    d.per_degree.assign(NET_DEGREE_COUNTS, 0);
    for (int i = 0; i < NET_DEGREE_COUNTS; i++) d.per_degree[i] = count_pick(rng);
    d.per_degree[3] = 0;  // degree 5 deliberately empty -- exercises the boundary-skip on a tie

    d.net_count.assign(NET_DEGREE_COUNTS, 0);
    int running = 0;
    for (int i = 0; i < NET_DEGREE_COUNTS; i++) {
        running += d.per_degree[i];
        d.net_count[i] = running;
    }
    d.num_nets = running;

    for (int i = 0; i < NET_DEGREE_COUNTS; i++) {
        const int degree = MIN_NET_DEGREE + i;
        for (int n = 0; n < d.per_degree[i]; n++)
            for (int k = 0; k < degree; k++)
                d.pin_x.push_back(pos(rng));
    }
    return d;
}

// Golden: independent max-min per net, walking the same degree-major layout.
static void golden(const AxisDesign& d, std::vector<float>& hpwl) {
    hpwl.assign(d.num_nets, 0.0f);
    int pin_offset = 0, n = 0;
    for (int i = 0; i < NET_DEGREE_COUNTS; i++) {
        const int degree = MIN_NET_DEGREE + i;
        for (int c = 0; c < d.per_degree[i]; c++) {
            float max_x = -1e30f, min_x = 1e30f;
            for (int k = 0; k < degree; k++) {
                const float x = d.pin_x[pin_offset + k];
                max_x = std::max(max_x, x);
                min_x = std::min(min_x, x);
            }
            hpwl[n] = max_x - min_x;
            pin_offset += degree;
            n++;
        }
    }
}

int main() {
    AxisDesign d = build_design(20260921u);

    std::vector<float> out_hpwl(d.num_nets, -1.0f);
    hpwl_gradient_dhar_v2(d.net_count.data(), d.pin_x.data(), nullptr, nullptr, nullptr,
                          nullptr, nullptr, out_hpwl.data(),
                          0.0f, 0.0f, 0, d.num_nets, 0, 0);

    std::vector<float> golden_hpwl;
    golden(d, golden_hpwl);

    printf("[info] %d nets, degree %d..%d, per_degree = [", d.num_nets, MIN_NET_DEGREE, LANES);
    for (int i = 0; i < NET_DEGREE_COUNTS; i++) printf("%d%s", d.per_degree[i], i + 1 < NET_DEGREE_COUNTS ? "," : "");
    printf("], cumulative net_count = [");
    for (int i = 0; i < NET_DEGREE_COUNTS; i++) printf("%d%s", d.net_count[i], i + 1 < NET_DEGREE_COUNTS ? "," : "");
    printf("]\n");

    int bad = 0;
    for (int n = 0; n < d.num_nets; n++) {
        if (out_hpwl[n] != golden_hpwl[n]) {
            if (bad < 5) printf("FAIL net %d: got %.6f golden %.6f\n", n, out_hpwl[n], golden_hpwl[n]);
            bad++;
        }
    }
    const bool ok = (bad == 0);
    if (bad) printf("FAIL: %d/%d nets mismatched\n", bad, d.num_nets);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
