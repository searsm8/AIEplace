// Verify iteration_update_CU (src/modules/iteration_update_CU.hpp) on synthetic chunked designs, from random
// state, BIT FOR BIT against:
//   - #47 M2 (host/sw_flat/borrowed_fusion.hpp fused_merge / fused_send) for the borrowed exchange, and
//   - sw_flat's per-slot expressions (placer/step.cpp combine_gradients, lipschitz_estimate, step_all_nodes,
//     enforce_die_boundaries; schedule.cpp update_precond_weights), transcribed below, for the rest.
// The real Placer holds the CU over whole iterations in host/sw_flat (make check-iteration-update).
//
//   [1] OPS     the beat-level golden (replay_fused_merge / _send) equals M2 for every owner; cycles [info]
//   [2] COMBINE every movable slot's combined gradient, both axes; the four norms equal the slot_reduce
//               order over the golden's terms, and lie within 1e-5 of a double sum
//   [3] STEP    every movable slot's node, probe and 1/precond; every borrowed slot equals M2's send of
//               the new probe positions; the two precond norms as in [2]
//   [4] MUTANTS the device given wrong op lists must differ: borrower ranks reversed, one merge entry
//               dropped, one borrower's merge ops skipped, one send BEAT dropped
// Meow.

#include "tier1_stub.hpp"   // before the module
#include "record_design.hpp"
#include "modules/iteration_update_CU.hpp"
#include "borrowed_fusion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

using namespace plalgo;
using pinrec::LANES;
using pinrec::SlotBeat;

namespace {

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::vector<SlotBeat> to_beats(const std::vector<float>& image) {
    std::vector<SlotBeat> beats(image.size() / LANES);
    std::memcpy(beats.data(), image.data(), image.size() * sizeof(float));
    return beats;
}
std::vector<float> to_floats(const std::vector<SlotBeat>& beats) {
    std::vector<float> image(beats.size() * LANES);
    std::memcpy(image.data(), beats.data(), image.size() * sizeof(float));
    return image;
}

// One design's slot image (chunks, then a filler tail) and its random state, both axes. Meow.
struct Fixture {
    packer::Chunked ch;
    packer::ChunkedDevice dev;          // desc only
    sw_flat::FusedDevice fused;
    std::vector<std::vector<sw_flat::BorrowedParcel>> parcels;
    long chunk_slots = 0, num_slots = 0, filler_beat_offset = 0, num_filler_beats = 0;
    std::vector<char> movable, component;      // per slot
    std::vector<long> movable_slots, borrowed_slots;
    std::vector<uint16_t> movable_mask;        // per beat
    std::vector<float> force[2], cur_node[2], cur_probe[2], cur_grad[2], next_probe[2], next_grad[2];
    std::vector<float> inv_precond, num_pins, area, width, height;
    UpdateScalars sc{};
};

Fixture make_fixture(const packer::Netlist& nl, long capacity, int fm_passes, int num_fillers, unsigned seed) {
    Fixture f;
    packer::Config cfg;
    cfg.partition_fm_passes = fm_passes;
    f.ch = packer::encode_chunked(nl, cfg, capacity);
    if (!f.ch.fits || packer::check_chunked(nl, f.ch, cfg)) { printf("FAIL chunked packer check\n"); exit(1); }
    f.dev = packer::chunked_device_arrays(f.ch, 0, std::vector<float>(nl.movable.size(), 0.0f));
    f.fused = sw_flat::fused_device_ops(f.ch);
    f.parcels = sw_flat::borrowed_parcels(f.ch);
    for (const packer::Chunk& c : f.ch.chunks) f.chunk_slots += c.enc.num_slots;
    f.filler_beat_offset = f.chunk_slots / LANES;
    f.num_filler_beats = (num_fillers + LANES - 1) / LANES;
    f.num_slots = f.chunk_slots + f.num_filler_beats * LANES;
    f.movable.assign(f.num_slots, 0);
    f.component.assign(f.num_slots, 0);
    long base = 0;
    for (const packer::Chunk& c : f.ch.chunks) {
        for (size_t l = 0; l < c.work_node.size(); l++) {
            if (c.enc.node_slot[l] < 0) continue;
            const long slot = base + c.enc.node_slot[l];
            if (c.borrowed[l]) f.borrowed_slots.push_back(slot);
            else if (c.enc.node_slot[l] < c.enc.first_borrowed_slot) f.movable[slot] = f.component[slot] = 1;
        }
        base += c.enc.num_slots;
    }
    for (long s = f.chunk_slots; s < f.chunk_slots + num_fillers; s++) f.movable[s] = 1;
    for (long s = 0; s < f.num_slots; s++) if (f.movable[s]) f.movable_slots.push_back(s);
    f.movable_mask.assign(f.num_slots / LANES, 0);
    for (long s = 0; s < f.num_slots; s++) if (f.movable[s]) f.movable_mask[s / LANES] |= (uint16_t)(1u << (s % LANES));

    // Mixed magnitudes, so a changed add order shows in the low bits; a non-movable slot is NaN where
    // the device must not depend on it, 0 where sw_flat leaves a defined value. Meow.
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f), decade(-3.0f, 3.0f), place(0.0f, 20000.0f), pos01(0.0f, 1.0f);
    auto mixed = [&] { return unit(rng) * std::pow(10.0f, decade(rng)); };
    for (int a = 0; a < 2; a++) {
        for (auto* v : {&f.force[a], &f.cur_node[a], &f.cur_probe[a], &f.cur_grad[a], &f.next_probe[a], &f.next_grad[a]}) v->assign(f.num_slots, 0.0f);
        for (long s : f.movable_slots) {
            f.force[a][s] = mixed();
            f.cur_node[a][s] = place(rng);
            f.cur_probe[a][s] = place(rng);
            f.cur_grad[a][s] = mixed();
            f.next_probe[a][s] = place(rng);
            f.next_grad[a][s] = f.component[s] ? mixed() : 0.0f;   // fillers: never written by the gradient CU
        }
        for (long s : f.borrowed_slots) { f.next_grad[a][s] = mixed(); f.next_probe[a][s] = NAN; }   // NaN: the send must fill them
    }
    f.inv_precond.assign(f.num_slots, 0.0f);
    f.num_pins.assign(f.num_slots, 0.0f);
    f.area.assign(f.num_slots, 0.0f);
    f.width.assign(f.num_slots, 0.0f);
    f.height.assign(f.num_slots, 0.0f);
    for (long s : f.movable_slots) {
        f.inv_precond[s] = 1.0f / (1.0f + 50.0f * pos01(rng));
        f.num_pins[s] = f.component[s] ? (float)(1 + rng() % 8) : 0.0f;
        f.width[s] = f.component[s] && rng() % 50 == 0 ? 6000.0f + 20000.0f * pos01(rng) : 20.0f + 200.0f * pos01(rng);   // a few wider than the die
        f.height[s] = 120.0f;
        f.area[s] = f.width[s] * f.height[s];
    }
    f.sc.step_length = 0.37f;
    f.sc.momentum_coeff = 0.81f;
    f.sc.lambda_area_coef = 1.3e-4f;
    f.sc.die_w = 21000.0f;
    f.sc.die_h = 20500.0f;
    f.sc.clamp_w = 150.0f * (float)M_SQRT2;
    f.sc.clamp_h = 150.0f * (float)M_SQRT2;
    f.sc.precond_enabled = 1;
    return f;
}

struct DeviceOut { std::vector<float> grad[2], node[2], probe[2], inv_precond; float norms[4]; };

DeviceOut run_device(const Fixture& f, int mode, const std::vector<pinrec::FusedOwnerDesc>& owners, const std::vector<uint64_t>& ops) {
    std::vector<SlotBeat> force[2], cn[2], cp[2], cg[2], nn[2], np[2], ng[2];
    for (int a = 0; a < 2; a++) {
        force[a] = to_beats(f.force[a]);
        cp[a] = to_beats(f.cur_probe[a]);
        cg[a] = to_beats(f.cur_grad[a]);
        np[a] = to_beats(f.next_probe[a]);
        ng[a] = to_beats(f.next_grad[a]);
        cn[a] = to_beats(f.cur_node[a]);
        nn[a] = to_beats(std::vector<float>(f.num_slots, NAN));
    }
    std::vector<SlotBeat> ipw = to_beats(f.inv_precond), pins = to_beats(f.num_pins), area = to_beats(f.area);
    std::vector<SlotBeat> width = to_beats(f.width), height = to_beats(f.height);
    DeviceOut out;
    iteration_update_CU(mode, f.ch.num_chunks, f.dev.desc.data(), owners.data(), ops.data(), (int)f.filler_beat_offset, (int)f.num_filler_beats,
                        f.movable_mask.data(), force[0].data(), force[1].data(), cn[0].data(), cn[1].data(), cp[0].data(), cp[1].data(),
                        cg[0].data(), cg[1].data(), nn[0].data(), nn[1].data(), np[0].data(), np[1].data(), ng[0].data(), ng[1].data(),
                        ipw.data(), pins.data(), area.data(), width.data(), height.data(), f.sc, out.norms);
    for (int a = 0; a < 2; a++) { out.grad[a] = to_floats(ng[a]); out.node[a] = to_floats(nn[a]); out.probe[a] = to_floats(np[a]); }
    out.inv_precond = to_floats(ipw);
    return out;
}

// slot_reduce's order over per-slot terms (0 off the movable slots), and a double sum. Meow.
float lanes_sum(const Fixture& f, const std::vector<float>& by_slot) {
    SlotReducer r;
    r.clear();
    auto add = [&](long first_beat, long beats) {
        for (long b = first_beat; b < first_beat + beats; b++) {
            float v[LANES];
            for (int j = 0; j < LANES; j++) v[j] = by_slot[b * LANES + j];
            r.add_beat(v);
        }
    };
    for (const pinrec::ChunkDesc& d : f.dev.desc) add(d.slot_beat_offset, d.first_borrowed_slot / LANES);
    add(f.filler_beat_offset, f.num_filler_beats);
    return r.total();
}
double double_sum(const std::vector<float>& by_slot) { double s = 0; for (float v : by_slot) s += v; return s; }

bool check_norm(const char* label, const char* name, const Fixture& f, float device, const std::vector<float>& terms) {
    const float model = lanes_sum(f, terms);
    const double reference = double_sum(terms);
    const double rel = std::fabs(device - reference) / std::max(1e-30, std::fabs(reference));
    const bool ok = same_bits(device, model) && rel < 1e-5;
    printf("%s       [%s] %-12s device %.9e  lanes model %s  rel vs double %.2e\n", ok ? "ok  " : "FAIL", label, name, device,
           same_bits(device, model) ? "equal" : "DIFFERS", rel);
    return ok;
}

long count_differences(const std::vector<float>& a, const std::vector<float>& b, const std::vector<long>& slots) {
    long differ = 0;
    for (long slot : slots) differ += !same_bits(a[slot], b[slot]);
    return differ;
}

bool run_config(const char* label, const packer::Netlist& nl, long capacity, int fm_passes, bool require_multi) {
    const Fixture f = make_fixture(nl, capacity, fm_passes, 200, 77u);
    long multi = 0;
    {
        std::vector<int> holders(f.num_slots, 0);
        for (const auto& owner : f.parcels) for (const auto& p : owner) for (long s : p.owner_slot) holders[s]++;
        for (int h : holders) multi += h >= 2;
    }
    int max_borrowers = 0;
    for (const auto& d : f.fused.owners) max_borrowers = std::max(max_borrowers, (int)d.num_borrowers);
    printf("     [%s] %d chunks, %zu movable, %zu borrowed slots, %ld slots with >= 2 borrowers, at most %d borrowers per owner\n",
           label, f.ch.num_chunks, f.movable_slots.size(), f.borrowed_slots.size(), multi, max_borrowers);
    bool ok = true;

    // [1] the beat-level golden vs M2. Meow.
    {
        long merge_differ = 0, send_differ = 0, merge_cycles = 0, send_cycles = 0, shared_beats = 0;
        std::vector<float> m2 = f.next_grad[0], beat = f.next_grad[0], m2_pos = f.cur_probe[0], beat_pos = f.cur_probe[0];
        for (long s : f.borrowed_slots) m2_pos[s] = beat_pos[s] = NAN;
        for (int j = 0; j < f.ch.num_chunks; j++) {
            sw_flat::fused_merge(f.ch, j, f.parcels[j], m2);
            merge_cycles += sw_flat::replay_fused_merge(f.ch, j, f.fused.per_owner[j], beat);
            sw_flat::fused_send(f.ch, j, f.parcels[j], m2_pos);
            send_cycles += sw_flat::replay_fused_send(f.ch, j, f.fused.per_owner[j], beat_pos);
            shared_beats += f.fused.per_owner[j].shared_beats;
        }
        merge_differ = count_differences(m2, beat, f.movable_slots);
        send_differ = count_differences(m2_pos, beat_pos, f.borrowed_slots);
        const bool ops_ok = merge_differ == 0 && send_differ == 0;
        printf("%s [1] [%s] beat-level golden vs M2: merge %ld differ, send %ld differ\n", ops_ok ? "ok  " : "FAIL", label, merge_differ, send_differ);
        printf("[info]   [%s] shared beats %ld: merge %ld cycles, send %ld cycles (%.2f / %.2f per shared beat)\n", label, shared_beats,
               merge_cycles, send_cycles, (double)merge_cycles / std::max(1L, shared_beats), (double)send_cycles / std::max(1L, shared_beats));
        ok &= ops_ok;
    }

    // [2] COMBINE. Golden: M2's merge, then sw_flat's expressions. Meow.
    std::vector<float> golden_grad[2];
    std::vector<float> pos_terms(f.num_slots, 0.0f), grad_terms(f.num_slots, 0.0f), gwl_terms(f.num_slots, 0.0f), gden_terms(f.num_slots, 0.0f);
    {
        for (int a = 0; a < 2; a++) {
            golden_grad[a] = f.next_grad[a];
            for (int j = 0; j < f.ch.num_chunks; j++) sw_flat::fused_merge(f.ch, j, f.parcels[j], golden_grad[a]);
        }
        for (long s : f.movable_slots) {
            const float gx = golden_grad[0][s], gy = golden_grad[1][s], ex = f.force[0][s], ey = f.force[1][s];
            gwl_terms[s] = f.component[s] ? fabsf(gx) + fabsf(gy) : 0.0f;
            gden_terms[s] = fabsf(ex) + fabsf(ey);
            golden_grad[0][s] = gx - ex;
            golden_grad[1][s] = gy - ey;
            const float dx = f.next_probe[0][s] - f.cur_probe[0][s], dy = f.next_probe[1][s] - f.cur_probe[1][s];
            pos_terms[s] = dx * dx + dy * dy;
            const float inv_pw = f.inv_precond[s];
            const float dgx = inv_pw * (golden_grad[0][s] - f.cur_grad[0][s]), dgy = inv_pw * (golden_grad[1][s] - f.cur_grad[1][s]);
            grad_terms[s] = dgx * dgx + dgy * dgy;
        }
        const DeviceOut d = run_device(f, UPDATE_COMBINE, f.fused.owners, f.fused.ops);
        const long differ = count_differences(d.grad[0], golden_grad[0], f.movable_slots) + count_differences(d.grad[1], golden_grad[1], f.movable_slots);
        printf("%s [2] [%s] COMBINE: %ld of %zu movable gradients differ (both axes)\n", differ ? "FAIL" : "ok  ", label, differ, 2 * f.movable_slots.size());
        ok &= differ == 0;
        ok &= check_norm(label, "pos_norm_sq", f, d.norms[0], pos_terms);
        ok &= check_norm(label, "grad_norm_sq", f, d.norms[1], grad_terms);
        ok &= check_norm(label, "gwl_L1", f, d.norms[2], gwl_terms);
        ok &= check_norm(label, "gden_L1", f, d.norms[3], gden_terms);

        // [4] COMBINE mutants. Meow.
        auto mutant = [&](const char* name, auto edit) {
            auto owners = f.fused.owners;
            auto ops = f.fused.ops;
            if (!edit(owners, ops)) { printf("FAIL [4] [%s] mutant %s: nothing to mutate\n", label, name); return false; }
            const DeviceOut m = run_device(f, UPDATE_COMBINE, owners, ops);
            const long n = count_differences(m.grad[0], golden_grad[0], f.movable_slots) + count_differences(m.grad[1], golden_grad[1], f.movable_slots);
            printf("%s [4] [%s] mutant %-28s %ld gradients differ\n", n ? "ok  " : "FAIL", label, name, n);
            return n > 0;
        };
        if (multi > 0 || require_multi)
            ok &= mutant("borrower ranks reversed", [&](auto& owners, auto& ops) {
                for (size_t j = 0; j < owners.size(); j++) {
                    const int nb = owners[j].num_borrowers;
                    if (nb < 2) continue;
                    for (int i = 0; i < owners[j].num_merge_ops; i++) {
                        uint64_t& op = ops[owners[j].merge_op_offset + i];
                        const int s = pinrec::fused_borrower(op);
                        op = (op & ~(7ull << pinrec::FUSED_BORROWER_SHIFT)) | (uint64_t)(nb - 1 - s) << pinrec::FUSED_BORROWER_SHIFT;
                    }
                    std::reverse(owners[j].block_beat_offset, owners[j].block_beat_offset + nb);
                    return true;
                }
                return false;
            });
        ok &= mutant("one merge entry dropped", [&](auto& owners, auto& ops) {
            for (const auto& d : owners)
                for (int i = d.num_merge_ops - 1; i >= 0; i--) {
                    uint64_t& op = ops[d.merge_op_offset + i];
                    if (op & pinrec::FUSED_BEAT) continue;
                    for (int e = pinrec::FUSED_GROUP_ENTRIES - 1; e >= 0; e--)
                        if (pinrec::fused_entry_valid(op, e)) { op &= ~(1ull << (e * pinrec::FUSED_ENTRY_BITS + 9)); return true; }
                }
            return false;
        });
        ok &= mutant("borrower 0's merge skipped", [&](auto& owners, auto& ops) {
            for (auto& d : owners) {
                if (d.num_borrowers < 1) continue;
                for (int i = 0; i < d.num_merge_ops; i++) {
                    uint64_t& op = ops[d.merge_op_offset + i];
                    if (pinrec::fused_borrower(op) != 0) continue;
                    op = (op & pinrec::FUSED_BEAT) ? 0 : (op & pinrec::FUSED_BOUNDARY);   // loads become no-ops, groups lose their entries
                }
                return true;
            }
            return false;
        });
    }

    // [3] STEP, from the combined state (the swap: current = COMBINE's next). Meow.
    {
        Fixture g = f;
        for (int a = 0; a < 2; a++) { g.cur_grad[a] = golden_grad[a]; g.cur_node[a] = f.cur_node[a]; g.cur_probe[a] = f.next_probe[a]; }
        std::vector<float> node[2], probe[2], inv_precond = g.inv_precond;
        std::vector<float> a1_terms(g.num_slots, 0.0f), a2_terms(g.num_slots, 0.0f);
        for (int a = 0; a < 2; a++) { node[a] = g.cur_node[a]; probe[a] = g.cur_probe[a]; }
        for (long s : g.movable_slots) {
            const float pins = g.num_pins[s], a2 = g.sc.lambda_area_coef * g.area[s];
            a1_terms[s] = pins;
            a2_terms[s] = a2;
            const float weight = std::max(1.0f, pins + a2);
            inv_precond[s] = 1.0f / weight;
            float n[2], p[2];
            for (int a = 0; a < 2; a++) {
                const float pg = inv_precond[s] * g.cur_grad[a][s];
                n[a] = g.cur_probe[a][s] - g.sc.step_length * pg;
                p[a] = n[a] + g.sc.momentum_coeff * (n[a] - g.cur_node[a][s]);
            }
            const float sizes[2] = {g.width[s], g.height[s]}, clamps[2] = {g.sc.clamp_w, g.sc.clamp_h}, dies[2] = {g.sc.die_w, g.sc.die_h};
            for (int a = 0; a < 2; a++) {
                const float cw = std::max(sizes[a], clamps[a]);
                float lo = 0.5f * (cw - sizes[a]), hi = dies[a] - 0.5f * (cw + sizes[a]);
                if (lo > hi) lo = hi = 0.5f * (dies[a] - sizes[a]);
                node[a][s] = std::clamp(n[a], lo, hi);
                probe[a][s] = std::clamp(p[a], lo, hi);
            }
        }
        std::vector<float> sent[2] = {probe[0], probe[1]};
        for (int a = 0; a < 2; a++) {
            for (long s : g.borrowed_slots) sent[a][s] = NAN;
            for (int j = 0; j < g.ch.num_chunks; j++) sw_flat::fused_send(g.ch, j, g.parcels[j], sent[a]);
        }
        // next_probe starts NaN: STEP must write every movable slot and every borrowed one. Meow.
        Fixture step_in = g;
        for (int a = 0; a < 2; a++) step_in.next_probe[a].assign(g.num_slots, NAN);
        const DeviceOut d = run_device(step_in, UPDATE_STEP, g.fused.owners, g.fused.ops);
        long node_differ = 0, probe_differ = 0, send_differ = 0;
        for (int a = 0; a < 2; a++) {
            node_differ += count_differences(d.node[a], node[a], g.movable_slots);
            probe_differ += count_differences(d.probe[a], probe[a], g.movable_slots);
            send_differ += count_differences(d.probe[a], sent[a], g.borrowed_slots);
        }
        const long ipw_differ = count_differences(d.inv_precond, inv_precond, g.movable_slots);
        const bool step_ok = node_differ == 0 && probe_differ == 0 && send_differ == 0 && ipw_differ == 0;
        printf("%s [3] [%s] STEP: node %ld, probe %ld, 1/precond %ld of %zu differ; borrowed %ld of %zu differ\n", step_ok ? "ok  " : "FAIL",
               label, node_differ, probe_differ, ipw_differ, g.movable_slots.size(), send_differ, 2 * g.borrowed_slots.size());
        ok &= step_ok;
        ok &= check_norm(label, "a1_norm", g, d.norms[0], a1_terms);
        ok &= check_norm(label, "a2_norm", g, d.norms[1], a2_terms);

        // [4] STEP mutants. A single dropped BEAT only delays the ring (the next BEAT writes the same
        // half-row), so the stream mutant drops ALL of one borrower's BEATs. Meow.
        auto send_mutant = [&](const char* name, auto edit) {
            auto ops = g.fused.ops;
            bool edited = false;
            for (const auto& o : g.fused.owners)
                if (!edited && o.num_borrowers > 0) { edited = edit(&ops[o.send_op_offset], o.num_send_ops); }
            const DeviceOut m = run_device(step_in, UPDATE_STEP, g.fused.owners, ops);
            long n = 0;
            for (int a = 0; a < 2; a++) n += count_differences(m.probe[a], sent[a], g.borrowed_slots);
            printf("%s [4] [%s] mutant %-28s %ld borrowed positions differ\n", edited && n ? "ok  " : "FAIL", label, name, n);
            return edited && n > 0;
        };
        ok &= send_mutant("borrower 0's send BEATs skipped", [](uint64_t* ops, int num_ops) {
            for (int i = 0; i < num_ops; i++)
                if ((ops[i] & pinrec::FUSED_BEAT) && pinrec::fused_borrower(ops[i]) == 0) ops[i] = 0;
            return true;
        });
        ok &= send_mutant("one send entry dropped", [](uint64_t* ops, int num_ops) {
            for (int i = 0; i < num_ops; i++)
                if (!(ops[i] & pinrec::FUSED_BEAT) && pinrec::fused_entry_valid(ops[i], 0)) { ops[i] &= ~(1ull << 9); return true; }
            return false;
        });
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    packer::Netlist nl;
    if (argc >= 3 && fixture::load_from_args(3, argv, nl)) {
        const long capacity = argc >= 4 ? std::atol(argv[3]) : SLOT_CAPACITY;
        const bool ok = run_config(nl.name.c_str(), nl, capacity, 1, false);
        printf(ok ? "PASS: iteration_update_CU\n" : "FAIL: iteration_update_CU\n");
        return ok ? 0 : 1;
    }
    fixture::SyntheticSpec spec;
    spec.max_large_degree = pinrec::MAX_LARGE_NET_DEGREE;
    nl = fixture::build_synthetic(20260922u, spec);
    fixture::add_large_net_cases(nl);
    bool ok = true;
    ok &= run_config("capacity=4096", nl, 4096, 1, false);
    // K=4, three borrowers per owner: the rank mutant must bite. Bare cuts this small give owners up to
    // 19 borrowers, past the device's FUSED_MAX_BORROWERS (real designs at 1 M slots have K <= 3). Meow.
    ok &= run_config("capacity=3600", nl, 3600, 1, true);
    printf(ok ? "PASS: iteration_update_CU\n" : "FAIL: iteration_update_CU\n");
    return ok ? 0 : 1;
}
