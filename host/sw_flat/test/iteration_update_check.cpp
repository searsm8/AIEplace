// iteration_update_check -- #45 step 6: iteration_update_CU (pl/src/modules/iteration_update_CU.hpp) vs sw_flat's
// own Placer::iteration_update, run in g++ on the same state. BIT FOR BIT.
//
//   iteration_update_check <config.toml> [first_iteration=5] [iterations=3]
//
// The config must turn on the record-stream arithmetic (wa_sums = "tree", grad_merge = "stream") and
// reductions = "lanes" (test/iteration_update_check.sh appends them). The placer runs to `first_iteration`'s
// gradient; then, per checked iteration, the device takes the placer's state and:
//   [1] COMBINE  every movable slot's combined gradient equals merge_borrowed_gradients + combine_gradients'
//                (borrowed gradients from the placer's own borrowed slots, through the fused merge), and the
//                four norms equal lipschitz_estimate's and combine_gradients' bit for bit
//   [2] STEP     after the placer's schedule, update_precond_weights and take_step, with the host's scalars,
//                written into the OLD current buffers (the [current, next] swap): every movable node and
//                probe position, every borrowed probe copy (the fused send), 1 / precond_weight, and the two
//                precond norms
// Then the placer runs the next gradient and field and the loop repeats. Meow.

#include "placer/placer.hpp"

#include "../../../pl/test/tier1_stub.hpp"   // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "modules/iteration_update_CU.hpp"
#include "borrowed_fusion.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using pinrec::LANES;
using pinrec::SlotBeat;

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::vector<SlotBeat> beats_of(const std::vector<float>& image) {
    std::vector<SlotBeat> beats(image.size() / LANES);
    std::memcpy(beats.data(), image.data(), image.size() * sizeof(float));
    return beats;
}
float slot_value(const std::vector<SlotBeat>& beats, long slot) { return beats[slot / LANES].v[slot % LANES]; }

template <class XY> std::vector<float> axis_of(const std::vector<XY>& v, int axis, long num_slots) {
    std::vector<float> out(num_slots, NAN);
    for (size_t s = 0; s < v.size() && (long)s < num_slots; s++) out[s] = axis ? v[s].y : v[s].x;
    return out;
}

struct Check {
    long differ = 0, total = 0;
    void count(float a, float b) { differ += !same_bits(a, b); total++; }
};

bool report(const char* what, const Check& c) {
    printf("%s %-34s %ld of %ld differ\n", c.differ ? "FAIL" : "ok  ", what, c.differ, c.total);
    return c.differ == 0;
}
bool report_norm(const char* what, float device, float golden) {
    const bool ok = same_bits(device, golden);
    printf("%s %-34s device %.9e  sw_flat %.9e\n", ok ? "ok  " : "FAIL", what, device, golden);
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: iteration_update_check <config.toml> [first_iteration] [iterations]\n"); return 2; }
    const int first = argc > 2 ? std::atoi(argv[2]) : 5;
    const int checks = argc > 3 ? std::atoi(argv[3]) : 3;
    sw_flat::Placer p;
    p.setup(sw_flat::read_placer_config(argv[1]));
    if (!p.cfg.grad_merge_stream || !p.cfg.wa_sums_tree || !p.cfg.reductions_lanes) {
        fprintf(stderr, "iteration_update_check: needs wa_sums = \"tree\", grad_merge = \"stream\", reductions = \"lanes\"\n");
        return 2;
    }

    const sw_flat::SlotLayout& L = p.layout;
    const long num_slots = L.num_slots;
    const packer::ChunkedDevice desc = packer::chunked_device_arrays(p.chunked, 0, std::vector<float>(p.chunked.global.kind.size(), 0.0f));
    const sw_flat::FusedDevice fused = sw_flat::fused_device_ops(p.chunked);
    const int filler_beat_offset = (int)(L.first_filler_slot / LANES), num_filler_beats = (int)((num_slots - L.first_filler_slot) / LANES);
    std::vector<uint16_t> movable_mask(num_slots / LANES, 0);
    std::vector<long> movable, borrowed;
    for (long s = 0; s < num_slots; s++) {
        if (L.role[s] == sw_flat::SlotRole::OWNED || L.role[s] == sw_flat::SlotRole::FILLER) {
            movable_mask[s / LANES] |= (uint16_t)(1u << (s % LANES));
            movable.push_back(s);
        }
        if (L.role[s] == sw_flat::SlotRole::BORROWED) borrowed.push_back(s);
    }
    long shared_beats = 0, merge_ops = 0, send_ops = 0, owned_beats = 0;
    for (int j = 0; j < p.chunked.num_chunks; j++) {
        shared_beats += fused.per_owner[j].shared_beats;
        merge_ops += fused.per_owner[j].merge.size();
        send_ops += fused.per_owner[j].send.size();
        owned_beats += desc.desc[j].first_borrowed_slot / LANES;
    }
    printf("[info] %d chunks, %zu movable, %zu borrowed slots; owned beats %ld (shared %ld), fillers %d beats; "
           "merge %ld ops, send %ld ops\n", p.chunked.num_chunks, movable.size(), borrowed.size(), owned_beats, shared_beats,
           num_filler_beats, merge_ops, send_ops);
    // Cycles per call: one per op plus one per private owned beat and filler beat. Meow.
    printf("[info] cycles per call: COMBINE %ld, STEP %ld, against %ld beats streamed\n",
           merge_ops + owned_beats - shared_beats + num_filler_beats, send_ops + owned_beats - shared_beats + num_filler_beats,
           owned_beats + num_filler_beats);

    std::vector<float> pins(num_slots, 0.0f), area(num_slots, 0.0f), width(num_slots, 0.0f), height(num_slots, 0.0f);
    for (int i = 0; i < (int)p.reduce_slots.size(); i++) {
        const long s = p.reduce_slots[i];
        pins[s] = i < p.filler_start ? p.num_pins[s] : 0.0f;   // update_precond_weights' rule
        area[s] = p.area[s];
    }
    for (long s : movable) { width[s] = L.width[s]; height[s] = L.height[s]; }
    const std::vector<SlotBeat> pins_b = beats_of(pins), area_b = beats_of(area), width_b = beats_of(width), height_b = beats_of(height);

    p.run_until(first);
    bool ok = true;
    for (int c = 0; c < checks; c++) {
        if (p.stop_reason != sw_flat::StopReason::RUNNING) { printf("FAIL the placer stopped before iteration %d\n", p.iteration); return 1; }
        printf("--- iteration %d ---\n", p.iteration);

        // Device state from the placer, at iteration k's gradient (owned: WL only; borrowed: unmerged). Meow.
        std::vector<SlotBeat> force[2], cur_node[2], cur_probe[2], cur_grad[2], next_node[2], next_probe[2], next_grad[2];
        std::vector<float> inv_pw(num_slots, 0.0f);
        for (long s : movable) inv_pw[s] = 1.0f / p.precond_weight[s];
        std::vector<SlotBeat> inv_pw_b = beats_of(inv_pw);
        for (int a = 0; a < 2; a++) {
            std::vector<float> f(num_slots, 0.0f);
            for (long s : movable) { const sw_flat::Gradient e = p.electrostatic_force(s); f[s] = a ? e.y : e.x; }
            force[a] = beats_of(f);
            cur_node[a] = beats_of(axis_of(p.current.node_pos, a, num_slots));
            cur_probe[a] = beats_of(axis_of(p.current.probe_pos, a, num_slots));
            cur_grad[a] = beats_of(axis_of(p.current.probe_grad, a, num_slots));
            next_node[a] = beats_of(axis_of(p.next.node_pos, a, num_slots));
            next_probe[a] = beats_of(axis_of(p.next.probe_pos, a, num_slots));
            next_grad[a] = beats_of(axis_of(p.next.probe_grad, a, num_slots));
        }
        float norms[4];
        plalgo::UpdateScalars sc{};
        plalgo::iteration_update_CU(plalgo::UPDATE_COMBINE, p.chunked.num_chunks, desc.desc.data(), fused.owners.data(), fused.ops.data(),
                                    filler_beat_offset, num_filler_beats, movable_mask.data(), force[0].data(), force[1].data(),
                                    cur_node[0].data(), cur_node[1].data(), cur_probe[0].data(), cur_probe[1].data(),
                                    cur_grad[0].data(), cur_grad[1].data(), next_node[0].data(), next_node[1].data(),
                                    next_probe[0].data(), next_probe[1].data(), next_grad[0].data(), next_grad[1].data(),
                                    inv_pw_b.data(), pins_b.data(), area_b.data(), width_b.data(), height_b.data(), sc, norms);

        // The golden: iteration_update's first three calls. Meow.
        p.merge_borrowed_gradients();
        p.combine_gradients();
        p.step_length = p.lipschitz_estimate();
        Check grad;
        for (long s : movable) { grad.count(slot_value(next_grad[0], s), p.next.probe_grad[s].x); grad.count(slot_value(next_grad[1], s), p.next.probe_grad[s].y); }
        ok &= report("[1] COMBINE gradients", grad);
        ok &= report_norm("[1] pos_norm_sq", norms[0], p.last_pos_norm_sq);
        ok &= report_norm("[1] grad_norm_sq", norms[1], p.last_grad_norm_sq);
        ok &= report_norm("[1] gwl_L1", norms[2], p.last_gwl_L1);
        ok &= report_norm("[1] gden_L1", norms[3], p.last_gden_L1);

        // The rest of iteration_update on the placer; the device's STEP takes its scalars. Meow.
        p.record_iteration_results();
        p.update_schedule();
        if (p.check_convergence()) { printf("FAIL the placer converged at iteration %d: pick an earlier window\n", p.iteration); return 1; }
        ++p.iteration;
        p.update_precond_weights();
        p.take_step();
        sc.step_length = p.step_length;
        sc.momentum_coeff = p.momentum_coeff;
        sc.lambda_area_coef = p.precond_coef * p.density_weight;
        sc.die_w = (float)p.die_w;
        sc.die_h = (float)p.die_h;
        sc.clamp_w = p.cfg.enable_density_clamp ? p.bin_w * (float)M_SQRT2 : 0.0f;
        sc.clamp_h = p.cfg.enable_density_clamp ? p.bin_h * (float)M_SQRT2 : 0.0f;
        sc.precond_enabled = p.cfg.enable_preconditioning;
        // The swap: STEP reads COMBINE's next as current and writes into COMBINE's current, whose borrowed
        // rows still hold the previous iteration's copies. Meow.
        plalgo::iteration_update_CU(plalgo::UPDATE_STEP, p.chunked.num_chunks, desc.desc.data(), fused.owners.data(), fused.ops.data(),
                                    filler_beat_offset, num_filler_beats, movable_mask.data(), force[0].data(), force[1].data(),
                                    next_node[0].data(), next_node[1].data(), next_probe[0].data(), next_probe[1].data(),
                                    next_grad[0].data(), next_grad[1].data(), cur_node[0].data(), cur_node[1].data(),
                                    cur_probe[0].data(), cur_probe[1].data(), cur_grad[0].data(), cur_grad[1].data(),
                                    inv_pw_b.data(), pins_b.data(), area_b.data(), width_b.data(), height_b.data(), sc, norms);
        Check node, probe, sent, ipw;
        for (long s : movable) {
            node.count(slot_value(cur_node[0], s), p.next.node_pos[s].x);   node.count(slot_value(cur_node[1], s), p.next.node_pos[s].y);
            probe.count(slot_value(cur_probe[0], s), p.next.probe_pos[s].x); probe.count(slot_value(cur_probe[1], s), p.next.probe_pos[s].y);
            ipw.count(slot_value(inv_pw_b, s), 1.0f / p.precond_weight[s]);
        }
        for (long s : borrowed) { sent.count(slot_value(cur_probe[0], s), p.next.probe_pos[s].x); sent.count(slot_value(cur_probe[1], s), p.next.probe_pos[s].y); }
        ok &= report("[2] STEP node positions", node);
        ok &= report("[2] STEP probe positions", probe);
        ok &= report("[2] STEP borrowed copies (send)", sent);
        ok &= report("[2] STEP 1 / precond_weight", ipw);
        ok &= report_norm("[2] a1_norm", norms[0], p.precond_a1_norm);
        ok &= report_norm("[2] a2_norm", norms[1], p.precond_a2_norm);

        p.compute_hpwl_partials();
        p.compute_electric_fields();
    }
    printf(ok ? "PASS: iteration_update_CU bit-exact vs sw_flat\n" : "FAIL: iteration_update_CU vs sw_flat\n");
    return ok ? 0 : 1;
}
