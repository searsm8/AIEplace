// record_stream_check -- #45 step 5b: sw_flat's record-stream arithmetic (placer/record_stream.cpp) vs
// hpwl_gradient_CU itself, run in g++ on the same positions. BIT FOR BIT.
//
//   record_stream_check <config.toml> [iterations=30]
//
// The config must turn on wa_sums = "tree", grad_merge = "stream", hpwl = "fixed" and wa_exp = "lut"
// (test/record_stream_check.sh appends them). Positions: the initial placement plus `iterations`
// steps, so nets have real spans. Then, at those probe positions, both axes:
//   [1] GRADIENT  every OWNED slot's WA gradient equals the CU's, bit for bit
//   [2] HPWL      stream_hpwl_fixed's int64 per axis equals the CU's hpwl_total_fixed
//   [3] CONTROL   sw_only's arithmetic (serial sums, file-order merge, same table) differs from the CU
//                 on some slots, so [1] can fail. Each switch alone is reported as [info].
// The CU's grad image starts NaN, so a slot it never writes cannot pass. Meow.

#include "placer/placer.hpp"

#include "../../../pl/test/tier1_stub.hpp"   // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "modules/hpwl_gradient_CU.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct DeviceAxis {
    std::vector<float> grad;   // per global slot (the layout's chunk part)
    int64_t hpwl_fixed = -1;
};

DeviceAxis run_device(const sw_flat::Placer& placer, int axis)
{
    std::vector<float> node_pos(placer.design.num_nodes());
    for (int node = 0; node < placer.design.num_nodes(); node++) {
        const long slot = placer.layout.home_slot[node];
        const sw_flat::Position p = slot < 0 ? sw_flat::Position(placer.design.node_x[node], placer.design.node_y[node])
                                             : placer.next.probe_pos[slot];
        node_pos[node] = axis ? p.y : p.x;
    }
    const packer::ChunkedDevice dev = packer::chunked_device_arrays(placer.chunked, axis, node_pos);
    std::vector<pinrec::RecordBeat> records(dev.records.size() / pinrec::LANES);
    std::memcpy(records.data(), dev.records.data(), dev.records.size() * sizeof(uint32_t));
    std::vector<pinrec::SlotBeat> pos(dev.slot_images.size() / pinrec::LANES);
    std::memcpy(pos.data(), dev.slot_images.data(), dev.slot_images.size() * sizeof(float));
    std::vector<pinrec::SlotBeat> grad(pos.size());
    for (auto& beat : grad) for (float& v : beat.v) v = NAN;
    DeviceAxis out;
    const float inv_lut_step = 1.0f / (sw_flat::PL_LUT_STEP_NORM * placer.gamma);
    plalgo::hpwl_gradient_CU(dev.desc.data(), dev.group_ends.data(), (int)dev.desc.size(), records.data(), pos.data(),
                             dev.macro_pins.data(), dev.borrowed_blocks.data(), dev.borrowed_ops.data(),
                             dev.offset_table.data(), (int)dev.offset_table.size(),
                             placer.exp_lut.data(), (int)placer.exp_lut.size(), grad.data(), dev.offset_bits,
                             placer.inv_gamma, inv_lut_step, out.hpwl_fixed);
    const float* slots = reinterpret_cast<const float*>(grad.data());
    out.grad.assign(slots, slots + grad.size() * pinrec::LANES);
    return out;
}

// OWNED slots whose gradient differs from the device's in any bit, either axis. Meow.
long count_differing(const sw_flat::Placer& placer, const DeviceAxis device[2], double* max_rel = nullptr)
{
    long differing = 0;
    for (long slot = 0; slot < placer.layout.first_filler_slot; slot++) {
        if (placer.layout.role[slot] != sw_flat::SlotRole::OWNED) continue;
        for (int axis = 0; axis < 2; axis++) {
            const float mine = axis ? placer.next.probe_grad[slot].y : placer.next.probe_grad[slot].x;
            const float theirs = device[axis].grad[slot];
            if (std::memcmp(&mine, &theirs, sizeof mine) == 0) continue;
            differing++;
            if (max_rel && theirs != 0) *max_rel = std::max(*max_rel, std::fabs((double)mine - theirs) / std::fabs((double)theirs));
        }
    }
    return differing;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: record_stream_check <config.toml> [iterations]\n"); return 2; }
    const int iterations = argc > 2 ? atoi(argv[2]) : 30;
    sw_flat::PlacerConfig cfg = sw_flat::read_placer_config(argv[1]);
    cfg.dump_schedule_trace = false;
    if (!(cfg.wa_sums_tree && cfg.grad_merge_stream && cfg.hpwl_fixed && cfg.wa_exp_lut)) {
        fprintf(stderr, "record_stream_check: the config must set wa_sums = \"tree\", grad_merge = \"stream\", hpwl = \"fixed\", wa_exp = \"lut\"\n");
        return 2;
    }
    sw_flat::Placer placer;
    placer.setup(cfg);
    placer.initialize_placement();
    placer.compute_hpwl_partials();
    placer.compute_electric_fields();
    placer.initialize_density_weight();
    while (placer.iteration < iterations && !placer.nan_detected) placer.perform_iteration();
    if (placer.nan_detected) { printf("FAIL %s: NaN before iteration %d\n", placer.design.name.c_str(), iterations); return 1; }

    placer.compute_hpwl_partials();   // the WA gradient alone: perform_iteration leaves the combined one
    const DeviceAxis device[2] = {run_device(placer, 0), run_device(placer, 1)};
    long owned = 0;
    for (long slot = 0; slot < placer.layout.first_filler_slot; slot++) owned += placer.layout.role[slot] == sw_flat::SlotRole::OWNED;

    int failures = 0;
    const long gradient_diff = count_differing(placer, device);
    printf("[1] GRADIENT  %ld of %ld owned slots x 2 axes differ from hpwl_gradient_CU (need 0)\n", gradient_diff, owned);
    failures += gradient_diff != 0;

    int64_t mine[2];
    placer.stream_hpwl_fixed(true, mine);
    for (int axis = 0; axis < 2; axis++) {
        const bool same = mine[axis] == device[axis].hpwl_fixed;
        printf("[2] HPWL      axis %d: sw_flat %lld, CU %lld %s\n", axis, (long long)mine[axis], (long long)device[axis].hpwl_fixed, same ? "" : "MISMATCH");
        failures += !same;
    }

    // [info] the HPWL three ways at these positions: the int64 stream sum, sw_only's metric (summed in
    // double since 2026-10-16; in float it read 0.39% low on bigblue4), and a double sum over the
    // stream's nets (the reference both are read against). Meow.
    {
        auto double_sum = [&](const std::vector<int>& nets) {
            double total = 0;
            for (int net : nets) {
                const int first = placer.design.net_first_pin[net], degree = placer.design.net_degree(net);
                if (degree == 0 || degree > placer.cfg.ignore_net_degree) continue;
                double lo[2] = {INFINITY, INFINITY}, hi[2] = {-INFINITY, -INFINITY};
                for (int i = 0; i < degree; i++) {
                    const sw_flat::Position p = sw_flat::pin_position(placer, first + i, true);
                    lo[0] = std::min(lo[0], (double)p.x); hi[0] = std::max(hi[0], (double)p.x);
                    lo[1] = std::min(lo[1], (double)p.y); hi[1] = std::max(hi[1], (double)p.y);
                }
                total += (hi[0] - lo[0]) + (hi[1] - lo[1]);
            }
            return total;
        };
        const double reference = double_sum(placer.stream.nets), by_name = double_sum(placer.nets_by_name);
        const double fixed = placer.stream_wirelength(true), floating = placer.total_wirelength(placer.cfg.ignore_net_degree, true);
        printf("[info]        HPWL vs a double sum over the stream's nets: int64 %+.2e, sw_only's metric %+.2e "
               "(sw_only's net set in double: %+.2e)\n",
               (fixed - reference) / reference, (floating - reference) / reference, (by_name - reference) / reference);
    }

    // [3] and the [info] rows: the same positions, other arithmetic. Meow.
    auto variant = [&](bool tree, bool stream, double& max_rel) {
        placer.cfg.wa_sums_tree = tree;
        placer.cfg.grad_merge_stream = stream;
        placer.compute_hpwl_partials();
        max_rel = 0;
        return count_differing(placer, device, &max_rel);
    };
    double max_rel;
    const long control = variant(false, false, max_rel);
    printf("[3] CONTROL   serial sums + file merge: %ld slot-axes differ (need > 0), max_rel %.2e\n", control, max_rel);
    failures += control == 0;
    long only = variant(true, false, max_rel);
    printf("[info]        tree sums + file merge:   %ld differ, max_rel %.2e\n", only, max_rel);
    only = variant(false, true, max_rel);
    printf("[info]        serial sums + stream merge: %ld differ, max_rel %.2e\n", only, max_rel);

    printf("%s %s: %d chunks, %ld owned slots, iteration %d\n", failures ? "FAIL" : "PASS", placer.design.name.c_str(),
           placer.chunked.num_chunks, owned, placer.iteration);
    return failures ? 1 : 0;
}
