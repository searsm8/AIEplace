// Verify hpwl_gradient_computer (bring_up/hpwl_gradient_computer) -- WA gradient per node from the
// static pin-record stream, pin->node summation by on-chip scatter-add (#41).
//
// Golden: wa_gradient_golden.hpp -- sw_only's WA partial in double, straight from the PARSED
// netlist, independent of slots, banks, pin nodes, offset tables, lane order and the merge. With
// large nets on it includes the 17..96-pin nets the encoder kept (enc.large_nets).
//
//   [1] STRUCTURE  module (float, trees, LUT) vs golden (double, same LUT); tolerance as
//                  hpwl_dhar_test [1]: rel_rms < 1e-5, max_rel < 1e-4. Both axes, large nets on
//                  in three packer configs (default | window=3 | hazard=8), plus default with
//                  large nets off.
//   [2] LUT BUDGET module vs golden with true exp(): rel_rms < 1e-4, max_rel < 1e-3 (as [2] there).
//   [3] ZERO       movable nodes with no encoded pin get exactly 0.
//   [4] HPWL       the per-net HPWL by-product, bit-exact (as hpwl_computer_v2_test).
//   [5] COVERAGE   macros really fold (their gradient is non-trivial), repeated nodes really merge,
//                  large nets carry real weight, and some have pads inside them (split nodes).
//
// `hpwl_gradient_computer_test --bookshelf DIR NAME` / `--def FILE NAME` runs [1]+[4] on a real
// design (random positions), large nets on. Meow.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "wa_gradient_golden.hpp"
#include "modules/hpwl_gradient_computer.hpp"

#include <cstdio>

using namespace plalgo;

struct DeviceAxis {
    std::vector<OutBeat> out;
    std::vector<float>   node_grad;   // per parsed node (movable ones meaningful)
};

static DeviceAxis run_axis(const packer::Encoded& enc, size_t num_parsed, int axis, const std::vector<float>& node_pos,
                           const golden::Lut& lut) {
    const long num_beats = (long)enc.issue.size();
    std::vector<pinrec::RecordBeat> records(num_beats);
    std::memcpy(records.data(), enc.records[axis].data(), num_beats * sizeof(pinrec::RecordBeat));
    const std::vector<float> image = packer::slot_positions(enc, axis, node_pos);
    std::vector<pinrec::SlotBeat> pos(enc.num_slots / pinrec::LANES);
    std::memcpy(pos.data(), image.data(), image.size() * sizeof(float));
    const std::vector<pinrec::MacroPinRef> refs = packer::macro_pin_refs(enc, axis);
    std::vector<pinrec::SlotBeat> grad(enc.first_fixed_slot / pinrec::LANES);
    DeviceAxis dev;
    dev.out.resize(num_beats);
    hpwl_gradient_computer(records.data(), (int)num_beats, enc.beat_count.data(), enc.span_beat_count.data(),
                           pos.data(), (int)pos.size(), refs.data(), (int)refs.size(),
                           enc.offset_table[axis].data(), (int)enc.offset_table[axis].size(),
                           lut.table.data(), lut.size, dev.out.data(), grad.data(), (int)enc.first_fixed_slot,
                           enc.offset_bits, 1.0f / golden::GAMMA, lut.inv_step);
    const float* grad_slots = reinterpret_cast<const float*>(grad.data());
    dev.node_grad.assign(num_parsed, 0.0f);
    for (size_t node = 0; node < num_parsed; node++)
        if (enc.node_slot[node] >= 0 && enc.node_slot[node] < enc.first_fixed_slot) dev.node_grad[node] = grad_slots[enc.node_slot[node]];
    return dev;
}

static std::vector<char> touched_nodes(const packer::Netlist& nl, const packer::Encoded& enc) {
    std::vector<char> touched(nl.movable.size(), 0);
    for (const auto& net : nl.nets) if (packer::in_scope(net)) for (const auto& pin : net) touched[pin.node] = 1;
    for (int net : enc.large_nets) for (const auto& pin : nl.nets[net]) touched[pin.node] = 1;
    return touched;
}

static bool run_config(const char* label, const packer::Netlist& nl, const packer::Config& cfg,
                       const std::vector<float> node_pos[2], const golden::Lut& lut, bool check_lut_budget) {
    const packer::Encoded enc = packer::encode_netlist(nl, cfg);
    if (packer::check(nl, enc, cfg)) { printf("FAIL [%s] packer check\n", label); return false; }
    if (enc.num_slots > SLOT_CAPACITY) {
        printf("[skip] [%s] %ld slots exceed on-chip capacity %d (chunking case)\n", label, enc.num_slots, SLOT_CAPACITY);
        return true;
    }
    const std::vector<char> touched = touched_nodes(nl, enc);
    bool ok = true;
    for (int axis = 0; axis < 2; axis++) {
        const DeviceAxis dev = run_axis(enc, nl.movable.size(), axis, node_pos[axis], lut);
        const golden::Err s = golden::compare(nl, dev.node_grad,
                                              golden::wa_gradient(nl, axis, node_pos[axis], lut, true, enc.large_nets));
        const bool s_ok = s.rel_rms < golden::STRUCT_RMS_TOL && s.max_rel < golden::STRUCT_MAX_TOL;
        printf("%s [1] [%s] axis %d structure: rel_rms=%.3e max_rel=%.3e\n", s_ok ? "ok  " : "FAIL", label, axis, s.rel_rms, s.max_rel);
        ok &= s_ok;
        if (check_lut_budget) {
            const golden::Err l = golden::compare(nl, dev.node_grad,
                                                  golden::wa_gradient(nl, axis, node_pos[axis], lut, false, enc.large_nets));
            const bool l_ok = l.rel_rms < golden::LUT_RMS_TOL && l.max_rel < golden::LUT_MAX_TOL;
            printf("%s [2] [%s] axis %d LUT budget: rel_rms=%.3e max_rel=%.3e\n", l_ok ? "ok  " : "FAIL", label, axis, l.rel_rms, l.max_rel);
            ok &= l_ok;
        }
        long hpwl_bad = 0;
        for (size_t position = 0; position < dev.out.size(); position++)
            for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
                const int net = enc.net_at[position * pinrec::MAX_NETS_PER_BEAT + k];
                const float want = net == packer::NO_NET ? 0.0f : golden::span(nl, net, axis, node_pos[axis]);
                hpwl_bad += !(dev.out[position].v[k] == want);
            }
        printf("%s [4] [%s] axis %d HPWL by-product: %ld mismatches\n", hpwl_bad ? "FAIL" : "ok  ", label, axis, hpwl_bad);
        ok &= hpwl_bad == 0;

        long untouched = 0, nonzero = 0;
        for (size_t node = 0; node < nl.movable.size(); node++)
            if (nl.movable[node] && !touched[node]) { untouched++; nonzero += dev.node_grad[node] != 0.0f; }
        if (nonzero) { printf("FAIL [3] [%s] axis %d: %ld of %ld untouched nodes non-zero\n", label, axis, nonzero, untouched); ok = false; }
    }
    return ok;
}

// Large nets with a pad between their first and last real beat (the case that stretches the extent).
static long nets_with_inner_pads(const packer::Encoded& enc) {
    long count = 0, first_real = -1, pads_since = 0;
    for (size_t position = (size_t)enc.beat_count.back(); position < enc.issue.size(); position++) {
        const int b = enc.issue[position];
        if (b == packer::BUBBLE) { pads_since += first_real >= 0; continue; }
        if (first_real < 0) { first_real = (long)position; pads_since = 0; }
        if (enc.beats[b].last_of_net) { count += pads_since > 0; first_real = -1; }
    }
    return count;
}

int main(int argc, char** argv) {
    packer::Netlist nl;
    const bool real = fixture::load_from_args(argc, argv, nl);
    if (!real) {
        fixture::SyntheticSpec spec;
        spec.max_large_degree = packer::IGNORE_NET_DEGREE;   // 17..100: every span group, and 97..100 left out
        nl = fixture::build_synthetic(20260922u, spec);
        fixture::add_large_net_cases(nl);
    }
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u, golden::EXTENT),
                                            fixture::random_positions(nl.movable.size(), 12u, golden::EXTENT)};
    const golden::Lut lut = golden::make_lut();
    packer::Config large;  large.large_nets = true;
    if (real) return run_config(nl.name.c_str(), nl, large, node_pos, lut, false) ? 0 : 1;

    bool ok = true;
    packer::Config narrow = large;  narrow.window = 3;
    packer::Config slow   = large;  slow.hazard = 8;
    ok &= run_config("default", nl, large, node_pos, lut, true);
    ok &= run_config("large nets off", nl, packer::Config(), node_pos, lut, false);
    ok &= run_config("window=3", nl, narrow, node_pos, lut, false);
    ok &= run_config("hazard=8", nl, slow, node_pos, lut, false);

    // [3] needs untouched movable nodes to exist; [5] needs macros, repeated nodes and large nets to matter.
    const packer::Encoded enc = packer::encode_netlist(nl, large);
    const std::vector<char> touched = touched_nodes(nl, enc);
    long untouched = 0, repeated = 0;
    for (size_t node = 0; node < nl.movable.size(); node++) untouched += nl.movable[node] && !touched[node];
    for (size_t n = 0; n < nl.nets.size(); n++)
        repeated += packer::in_scope(nl.nets[n]) && enc.unique_nodes[n].size() < nl.nets[n].size();
    const std::vector<double> golden_x       = golden::wa_gradient(nl, 0, node_pos[0], lut, true, enc.large_nets);
    const std::vector<double> golden_x_small = golden::wa_gradient(nl, 0, node_pos[0], lut, true);
    double macro_share = 0, large_share = 0, total = 0;
    for (size_t node = 0; node < nl.movable.size(); node++) {
        if (!nl.movable[node]) continue;
        total += golden_x[node] * golden_x[node];
        if (nl.is_macro[node]) macro_share += golden_x[node] * golden_x[node];
        const double large_part = golden_x[node] - golden_x_small[node];
        large_share += large_part * large_part;
    }
    // Large nets carry ~0.7% here (macros dominate). A relative error e in their terms moves [1]'s
    // rel_rms by ~e*sqrt(share), so 0.1% still exposes e > ~3e-4 at the 1e-5 tolerance. Meow.
    const long inner_pads = nets_with_inner_pads(enc);
    const bool covered = untouched >= 20 && repeated >= 50 && macro_share > 0.01 * total &&
                         enc.large_nets.size() >= 50 && large_share > 0.001 * total && inner_pads >= 1;
    printf("%s [3/5] coverage: %ld untouched movable nodes, %ld nets with a repeated node, macros carry %.1f%% of |grad|^2, "
           "%zu large nets carry %.1f%%, %ld with pads inside\n", covered ? "ok  " : "FAIL", untouched, repeated,
           100.0 * macro_share / total, enc.large_nets.size(), 100.0 * large_share / total, inner_pads);
    ok &= covered;

    printf(ok ? "PASS: hpwl_gradient_computer\n" : "FAIL: hpwl_gradient_computer\n");
    return ok ? 0 : 1;
}
