// Verify hpwl_computer_v2 (bring_up/hpwl_computer_v2) -- per-net HPWL from the static pin-record
// stream with positions gathered from on-chip banked URAM (#41).
//
// Golden: each in-scope net's span max(pos + offset) - min(pos + offset), computed in float straight
// from the PARSED netlist and parsed-node positions -- independent of slots, banks, pin nodes and
// offset tables. The module computes the same float additions (pos + offset, macro pins via the
// on-chip refresh, fixed pins pre-added by the host), and max/min/sub are exact, so the comparison
// is BIT-EXACT. MACRO_PIN slots arrive as NaN, so a missed refresh cannot pass.
//
//   [1] every net position of every beat, both axes, large nets (17..96 pins) on, three packer
//       configs: default | window=3 (forces part-EMPTY beats mid-group) | hazard=8; plus default
//       with large nets off. Every encoded net must be reported exactly once.
//   [2] coverage: the design really exercises macro pins, fixed pins, repeated nodes, EMPTY nets,
//       several large-net spans, spans beyond ceil(degree/16), and 97..100-pin nets left out
//
// `hpwl_computer_v2_test --bookshelf DIR NAME` / `--def FILE NAME` runs [1] on a real design
// instead (random positions), for evidence; tier-1 (`make test`) runs the synthetic design. Meow.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "record_design.hpp"
#include "modules/hpwl_computer_v2.hpp"

#include <cstdio>

using namespace plalgo;

struct AxisRun {
    std::vector<OutBeat> out;
};

static std::vector<OutBeat> run_axis(const packer::Encoded& enc, int axis, const std::vector<float>& node_pos) {
    const long num_beats = (long)enc.issue.size();
    std::vector<pinrec::RecordBeat> records(num_beats);
    std::memcpy(records.data(), enc.records[axis].data(), num_beats * sizeof(pinrec::RecordBeat));
    const std::vector<float> image = packer::slot_positions(enc, axis, node_pos);
    std::vector<pinrec::SlotBeat> pos(enc.num_slots / pinrec::LANES);
    std::memcpy(pos.data(), image.data(), image.size() * sizeof(float));
    const std::vector<pinrec::MacroPinRef> refs = packer::macro_pin_refs(enc, axis);
    std::vector<OutBeat> out(num_beats);
    hpwl_computer_v2(records.data(), (int)num_beats, enc.beat_count.data(), enc.span_beat_count.data(), pos.data(), (int)pos.size(),
                     refs.data(), (int)refs.size(), enc.offset_table[axis].data(), (int)enc.offset_table[axis].size(),
                     out.data(), enc.offset_bits);
    return out;
}

static float golden_span(const packer::Netlist& nl, const std::vector<packer::Pin>& net, int axis,
                         const std::vector<float>& node_pos) {
    float hi = -INFINITY, lo = INFINITY;
    for (const packer::Pin& pin : net) {
        const float p = fixture::pin_position(nl, pin, axis, node_pos);
        hi = p > hi ? p : hi;
        lo = p < lo ? p : lo;
    }
    return hi - lo;
}

// Returns the number of mismatches; counts EMPTY net positions and reported nets (axis 0).
static long verify(const packer::Netlist& nl, const packer::Encoded& enc,
                   const std::vector<float> node_pos[2], long& empty_positions, long& nets_seen) {
    long bad = 0;
    empty_positions = nets_seen = 0;
    for (int axis = 0; axis < 2; axis++) {
        const std::vector<OutBeat> out = run_axis(enc, axis, node_pos[axis]);
        for (size_t position = 0; position < out.size(); position++) {
            for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
                const int net = enc.net_at[position * pinrec::MAX_NETS_PER_BEAT + k];
                const float got = out[position].v[k];
                const float want = net == packer::NO_NET ? 0.0f : golden_span(nl, nl.nets[net], axis, node_pos[axis]);
                if (net == packer::NO_NET && k < pinrec::LANES / enc.issue_degree[position]) empty_positions += axis == 0;
                nets_seen += net != packer::NO_NET && axis == 0;
                if (!(got == want)) {
                    if (bad < 5) printf("  FAIL axis %d position %zu net slot %d (net %d): got %.9g want %.9g\n",
                                        axis, position, k, net, got, want);
                    bad++;
                }
            }
        }
    }
    return bad;
}

static bool run_config(const char* label, const packer::Netlist& nl, const packer::Config& cfg,
                       const std::vector<float> node_pos[2], long* empty_out = nullptr) {
    const packer::Encoded enc = packer::encode_netlist(nl, cfg);
    if (packer::check(nl, enc, cfg)) { printf("FAIL [%s] packer check\n", label); return false; }
    if (enc.num_slots > SLOT_CAPACITY) {
        printf("[skip] [%s] %ld slots exceed on-chip capacity %d (chunking case)\n", label, enc.num_slots, SLOT_CAPACITY);
        return true;
    }
    long empty_positions = 0, nets_seen = 0;
    const long bad = verify(nl, enc, node_pos, empty_positions, nets_seen);
    long in_scope = 0;
    for (const auto& net : nl.nets) in_scope += packer::in_scope(net);
    const long expected = in_scope + (long)enc.large_nets.size();
    const bool ok = bad == 0 && nets_seen == expected;
    printf("%s [%s] %ld beats, %ld small + %zu large nets (%ld large dropped) x 2 axes, %ld reported, "
           "%ld EMPTY net positions, %ld mismatches\n", ok ? "ok  " : "FAIL", label, (long)enc.issue.size(),
           in_scope, enc.large_nets.size(), enc.large_dropped, nets_seen, empty_positions, bad);
    if (empty_out) *empty_out = empty_positions;
    return ok;
}

int main(int argc, char** argv) {
    packer::Netlist nl;
    const bool real = fixture::load_from_args(argc, argv, nl);
    fixture::SyntheticSpec spec;
    spec.max_large_degree = packer::IGNORE_NET_DEGREE;   // reach 97..100, which must be left out
    if (!real) nl = fixture::build_synthetic(20260922u, spec);
    std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u),
                                      fixture::random_positions(nl.movable.size(), 12u)};
    bool ok = true;

    packer::Config large;   large.large_nets = true;
    if (real) {
        ok = run_config(nl.name.c_str(), nl, large, node_pos);
        const packer::Encoded enc = packer::encode_netlist(nl, large);
        long large_beats = 0, min_large_beats = 0;
        for (int b : enc.issue) large_beats += b != packer::BUBBLE && enc.beats[b].span > 1;
        for (int net : enc.large_nets) min_large_beats += ((long)nl.nets[net].size() + pinrec::LANES - 1) / pinrec::LANES;
        printf("[info] large nets: %ld beats vs %ld at ceil(degree/16); %ld of %zu nets need extra beats\n",
               large_beats, min_large_beats, enc.large_extra_span, enc.large_nets.size());
        return ok ? 0 : 1;
    }

    packer::Config narrow = large;  narrow.window = 3;
    packer::Config slow   = large;  slow.hazard = 8;
    long empty_narrow = 0;
    ok &= run_config("default", nl, large, node_pos);
    ok &= run_config("large nets off", nl, packer::Config(), node_pos);
    ok &= run_config("window=3", nl, narrow, node_pos, &empty_narrow);
    ok &= run_config("hazard=8", nl, slow, node_pos);

    // [2] coverage -- a fixture that quietly lost a feature would make [1] vacuous for it
    const packer::Encoded enc = packer::encode_netlist(nl, large);
    long kind_count[5] = {}, repeated = 0, out_of_scope = 0;
    for (size_t node = 0; node < enc.kind.size(); node++) kind_count[enc.kind[node]]++;
    for (size_t n = 0; n < nl.nets.size(); n++) {
        out_of_scope += !packer::in_scope(nl.nets[n]);
        repeated += packer::in_scope(nl.nets[n]) && enc.unique_nodes[n].size() < nl.nets[n].size();
    }
    const bool covered = kind_count[packer::MACRO_PIN] >= 50 && kind_count[packer::FIXED_PIN] >= 50 &&
                         repeated >= 50 && out_of_scope >= 20 && empty_narrow >= 20;
    printf("%s [2] coverage: %ld macro-pin slots, %ld fixed-pin slots, %ld nets with a repeated node, "
           "%ld out-of-scope nets, %ld EMPTY net positions (window=3)\n", covered ? "ok  " : "FAIL",
           kind_count[packer::MACRO_PIN], kind_count[packer::FIXED_PIN], repeated, out_of_scope, empty_narrow);
    ok &= covered;

    long large_in_range = 0, over_96 = 0, spans_used = 0;
    for (const auto& net : nl.nets) {
        large_in_range += packer::is_large(net);
        over_96 += (int)net.size() > pinrec::MAX_LARGE_NET_DEGREE;
    }
    std::vector<long> nets_per_span(pinrec::MAX_SPAN + 1, 0);
    for (int b : enc.issue) if (b != packer::BUBBLE && enc.beats[b].last_of_net) nets_per_span[enc.beats[b].span]++;
    for (long count : nets_per_span) spans_used += count > 0;
    bool over_96_encoded = false;
    for (int net : enc.large_nets) over_96_encoded |= (int)nl.nets[net].size() > pinrec::MAX_LARGE_NET_DEGREE;
    const bool large_covered = (long)enc.large_nets.size() >= 50 && spans_used >= 5 && enc.large_extra_span >= 5 &&
                               over_96 >= 2 && !over_96_encoded &&
                               (long)enc.large_nets.size() + enc.large_dropped == large_in_range;
    printf("%s [2] large nets: %zu encoded over %ld spans, %ld needing extra beats, %ld dropped (> MAX_SPAN), "
           "%ld of 97..100 pins left out\n", large_covered ? "ok  " : "FAIL", enc.large_nets.size(), spans_used,
           enc.large_extra_span, enc.large_dropped, over_96);
    ok &= large_covered;

    printf(ok ? "PASS: hpwl_computer_v2\n" : "FAIL: hpwl_computer_v2\n");
    return ok ? 0 : 1;
}
