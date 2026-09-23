// Verify hpwl_gradient_computer (bring_up/hpwl_gradient_computer) -- WA gradient per node from the
// static pin-record stream, pin->node summation by on-chip scatter-add (#41).
//
// Golden: sw_only's WA partial (computeHpwlPartials_CPU, Partials.cpp; same form as
// hpwl_dhar_test's golden) evaluated in double straight from the PARSED netlist -- per in-scope
// net, bbox-shifted exponents, B/C sums, eq. 4 per pin, summed per parsed node. A macro's pins sum
// into the macro, fixed pins contribute nothing. Independent of slots, banks, pin nodes, offset
// tables, lane order and the merge.
//
//   [1] STRUCTURE  module (float, trees, LUT) vs golden (double, same LUT); tolerance as
//                  hpwl_dhar_test [1]: rel_rms < 1e-5, max_rel < 1e-4. Both axes, three packer
//                  configs (default | window=3 | hazard=8).
//   [2] LUT BUDGET module vs golden with true exp(): rel_rms < 1e-4, max_rel < 1e-3 (as [2] there).
//   [3] ZERO       movable nodes with no in-scope pin get exactly 0.
//   [4] HPWL       the per-net HPWL by-product, bit-exact (as hpwl_computer_v2_test).
//   [5] COVERAGE   macros really fold (their gradient is non-trivial), repeated nodes really merge.
//
// `hpwl_gradient_computer_test --bookshelf DIR NAME` / `--def FILE NAME` runs [1]+[4] on a real
// design (random positions). Meow.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "record_design.hpp"
#include "modules/hpwl_gradient_computer.hpp"

#include <cmath>
#include <cstdio>

using namespace plalgo;

// Production LUT geometry (host/src/pl_algo/src/main.cpp, Placement.hpp), as hpwl_dhar_test.
static constexpr float PLACE_STEP_NORM = 0.05f;
static constexpr int   GAMMA_MULT      = 12;
static constexpr float GAMMA           = 120.0f;
static constexpr float EXTENT          = 4000.0f;   // positions ~33 gamma across: exps span the LUT

struct Lut {
    std::vector<float> table;
    int   size;
    float inv_step;
};

static Lut make_lut() {
    Lut lut;
    lut.size = (int)(GAMMA_MULT / PLACE_STEP_NORM) + 2;
    for (int i = 0; i < lut.size; i++) lut.table.push_back(std::exp(-(float)i * PLACE_STEP_NORM));
    lut.inv_step = 1.0f / (PLACE_STEP_NORM * GAMMA);
    return lut;
}

struct DeviceAxis {
    std::vector<OutBeat> out;
    std::vector<float>   grad_slots;   // [first_fixed_slot]
};

static DeviceAxis run_axis(const packer::Encoded& enc, int axis, const std::vector<float>& node_pos, const Lut& lut) {
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
    hpwl_gradient_computer(records.data(), (int)num_beats, enc.beat_count.data(), pos.data(), (int)pos.size(),
                           refs.data(), (int)refs.size(), enc.offset_table[axis].data(), (int)enc.offset_table[axis].size(),
                           lut.table.data(), lut.size, dev.out.data(), grad.data(), (int)enc.first_fixed_slot,
                           enc.offset_bits, 1.0f / GAMMA, lut.inv_step);
    dev.grad_slots.resize(enc.first_fixed_slot);
    std::memcpy(dev.grad_slots.data(), grad.data(), dev.grad_slots.size() * sizeof(float));
    return dev;
}

// Independent re-implementation of the LUT interpolation, in double (as hpwl_dhar_test). Meow.
static double lut_exp_ref(const Lut& lut, float d) {
    const float idx_f = d * lut.inv_step;
    const int   idx   = (int)idx_f;
    if (idx >= lut.size - 1) return 0.0;
    const double frac = idx_f - idx;
    return (double)lut.table[idx] * (1.0 - frac) + (double)lut.table[idx + 1] * frac;
}

static std::vector<double> golden_grad(const packer::Netlist& nl, int axis, const std::vector<float>& node_pos,
                                       const Lut& lut, bool use_lut) {
    const double inv_gamma = 1.0 / GAMMA;
    auto expo = [&](float d) { return use_lut ? lut_exp_ref(lut, d) : std::exp(-(double)d * inv_gamma); };
    std::vector<double> grad(nl.movable.size(), 0.0);
    for (const auto& net : nl.nets) {
        if (!packer::in_scope(net)) continue;
        std::vector<float> p;
        for (const packer::Pin& pin : net) p.push_back(fixture::pin_position(nl, pin, axis, node_pos));
        const float hi = *std::max_element(p.begin(), p.end()), lo = *std::min_element(p.begin(), p.end());
        std::vector<double> ap(p.size()), am(p.size());
        double Bp = 0, Cp = 0, Bm = 0, Cm = 0;
        for (size_t k = 0; k < p.size(); k++) {
            ap[k] = expo(hi - p[k]); am[k] = expo(p[k] - lo);
            Bp += ap[k]; Cp += ap[k] * p[k]; Bm += am[k]; Cm += am[k] * p[k];
        }
        for (size_t k = 0; k < p.size(); k++) {
            if (!nl.movable[net[k].node]) continue;
            const double x = p[k];
            grad[net[k].node] += ((1.0 + x * inv_gamma) * Bp - Cp * inv_gamma) * (ap[k] / (Bp * Bp))
                               - ((1.0 - x * inv_gamma) * Bm + Cm * inv_gamma) * (am[k] / (Bm * Bm));
        }
    }
    return grad;
}

struct Err { double rel_rms, max_rel; };

static Err compare(const packer::Netlist& nl, const packer::Encoded& enc, const std::vector<float>& grad_slots,
                   const std::vector<double>& golden) {
    double se = 0, sr = 0, worst = 0; long count = 0;
    for (size_t node = 0; node < nl.movable.size(); node++) {
        if (!nl.movable[node]) continue;
        const double got = grad_slots[enc.node_slot[node]];
        const double err = got - golden[node];
        se += err * err; sr += golden[node] * golden[node]; count++;
        worst = std::max(worst, std::fabs(err));
        if (std::isnan(got)) worst = INFINITY;
    }
    const double rms_ref = std::sqrt(sr / count);
    return Err{std::sqrt(se / count) / rms_ref, worst / rms_ref};
}

static long hpwl_mismatches(const packer::Netlist& nl, const packer::Encoded& enc, const std::vector<OutBeat>& out,
                            int axis, const std::vector<float>& node_pos) {
    long bad = 0;
    for (size_t position = 0; position < out.size(); position++)
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            const int net = enc.net_at[position * pinrec::MAX_NETS_PER_BEAT + k];
            float want = 0.0f;
            if (net != packer::NO_NET) {
                float hi = -INFINITY, lo = INFINITY;
                for (const packer::Pin& pin : nl.nets[net]) {
                    const float p = fixture::pin_position(nl, pin, axis, node_pos);
                    hi = p > hi ? p : hi; lo = p < lo ? p : lo;
                }
                want = hi - lo;
            }
            bad += !(out[position].v[k] == want);
        }
    return bad;
}

static bool run_config(const char* label, const packer::Netlist& nl, const packer::Config& cfg,
                       const std::vector<float> node_pos[2], const Lut& lut, bool check_lut_budget) {
    const packer::Encoded enc = packer::encode_netlist(nl, cfg);
    if (packer::check(nl, enc, cfg)) { printf("FAIL [%s] packer check\n", label); return false; }
    if (enc.num_slots > SLOT_CAPACITY) {
        printf("[skip] [%s] %ld slots exceed on-chip capacity %d (chunking case)\n", label, enc.num_slots, SLOT_CAPACITY);
        return true;
    }
    bool ok = true;
    for (int axis = 0; axis < 2; axis++) {
        const DeviceAxis dev = run_axis(enc, axis, node_pos[axis], lut);
        const Err s = compare(nl, enc, dev.grad_slots, golden_grad(nl, axis, node_pos[axis], lut, true));
        const bool s_ok = s.rel_rms < 1e-5 && s.max_rel < 1e-4;
        printf("%s [1] [%s] axis %d structure: rel_rms=%.3e max_rel=%.3e\n", s_ok ? "ok  " : "FAIL", label, axis, s.rel_rms, s.max_rel);
        ok &= s_ok;
        if (check_lut_budget) {
            const Err l = compare(nl, enc, dev.grad_slots, golden_grad(nl, axis, node_pos[axis], lut, false));
            const bool l_ok = l.rel_rms < 1e-4 && l.max_rel < 1e-3;
            printf("%s [2] [%s] axis %d LUT budget: rel_rms=%.3e max_rel=%.3e\n", l_ok ? "ok  " : "FAIL", label, axis, l.rel_rms, l.max_rel);
            ok &= l_ok;
        }
        const long hpwl_bad = hpwl_mismatches(nl, enc, dev.out, axis, node_pos[axis]);
        printf("%s [4] [%s] axis %d HPWL by-product: %ld mismatches\n", hpwl_bad ? "FAIL" : "ok  ", label, axis, hpwl_bad);
        ok &= hpwl_bad == 0;

        // [3] exact zero for movable nodes that no in-scope net touches
        std::vector<char> touched(nl.movable.size(), 0);
        for (const auto& net : nl.nets) if (packer::in_scope(net)) for (const auto& pin : net) touched[pin.node] = 1;
        long untouched = 0, nonzero = 0;
        for (size_t node = 0; node < nl.movable.size(); node++)
            if (nl.movable[node] && !touched[node]) { untouched++; nonzero += dev.grad_slots[enc.node_slot[node]] != 0.0f; }
        if (nonzero) { printf("FAIL [3] [%s] axis %d: %ld of %ld untouched nodes non-zero\n", label, axis, nonzero, untouched); ok = false; }
    }
    return ok;
}

int main(int argc, char** argv) {
    packer::Netlist nl;
    const bool real = fixture::load_from_args(argc, argv, nl);
    if (!real) nl = fixture::build_synthetic(20260922u);
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u, EXTENT),
                                            fixture::random_positions(nl.movable.size(), 12u, EXTENT)};
    const Lut lut = make_lut();
    if (real) return run_config(nl.name.c_str(), nl, packer::Config(), node_pos, lut, false) ? 0 : 1;

    bool ok = true;
    packer::Config narrow; narrow.window = 3;
    packer::Config slow;   slow.hazard = 8;
    ok &= run_config("default", nl, packer::Config(), node_pos, lut, true);
    ok &= run_config("window=3", nl, narrow, node_pos, lut, false);
    ok &= run_config("hazard=8", nl, slow, node_pos, lut, false);

    // [3] needs untouched movable nodes to exist; [5] needs macros and repeated nodes to matter.
    const packer::Encoded enc = packer::encode_netlist(nl, packer::Config());
    std::vector<char> touched(nl.movable.size(), 0);
    for (const auto& net : nl.nets) if (packer::in_scope(net)) for (const auto& pin : net) touched[pin.node] = 1;
    long untouched = 0, repeated = 0;
    for (size_t node = 0; node < nl.movable.size(); node++) untouched += nl.movable[node] && !touched[node];
    for (size_t n = 0; n < nl.nets.size(); n++)
        repeated += packer::in_scope(nl.nets[n]) && enc.unique_nodes[n].size() < nl.nets[n].size();
    const std::vector<double> golden_x = golden_grad(nl, 0, node_pos[0], lut, true);
    double macro_share = 0, total = 0;
    for (size_t node = 0; node < nl.movable.size(); node++) {
        if (!nl.movable[node]) continue;
        total += golden_x[node] * golden_x[node];
        if (nl.is_macro[node]) macro_share += golden_x[node] * golden_x[node];
    }
    const bool covered = untouched >= 20 && repeated >= 50 && macro_share > 0.01 * total;
    printf("%s [3/5] coverage: %ld untouched movable nodes, %ld nets with a repeated node, macros carry %.1f%% of |grad|^2\n",
           covered ? "ok  " : "FAIL", untouched, repeated, 100.0 * macro_share / total);
    ok &= covered;

    printf(ok ? "PASS: hpwl_gradient_computer\n" : "FAIL: hpwl_gradient_computer\n");
    return ok ? 0 : 1;
}
