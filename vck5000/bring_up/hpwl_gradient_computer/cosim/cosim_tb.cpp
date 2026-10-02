// cosim_tb.cpp -- RTL co-simulation test bench for hpwl_gradient_computer_top (#41).
//
// Tier 1 runs the module as sequential C, so it cannot see a read-add-write race: the scatter-add
// and the macro fold are correct in C for ANY schedule. Only the RTL pipeline can race. This bench
// runs the synthesized top on a small synthetic design encoded with a given packer hazard:
//
//   argv[1] = hazard the packer schedules to (default HAZARD_DISTANCE = 4, what HLS was told)
//
//   hazard 4  -> expected PASS in C and in RTL (the contract)
//   hazard 1  -> expected PASS in C (no pipeline) and FAIL in RTL if a node's updates land closer
//                than the RMW round trip -- the negative control that shows cosim has teeth.
//
// Large nets are on, and two hand-built nets put one cell's pins in several beats of one net
// (runs HAZARD_DISTANCE apart, pads between). The 64-pin one has extent 13 at hazard 4, so the
// beat FIFOs must hold it while stage B waits on its bbox: built with -DPL_BEAT_FIFO_DEPTH below
// that, the RTL deadlocks (C simulation cannot), which is the depth sweep's negative control.
//
// Same golden and tolerance as tier 1 (wa_gradient_golden.hpp). Exits 0 on pass. Meow.

#include "wa_gradient_golden.hpp"
#include "modules/hpwl_gradient_computer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void hpwl_gradient_computer_top(
        const pinrec::RecordBeat*, int, const int*, const int*, const pinrec::SlotBeat*, int, const pinrec::MacroPinRef*, int,
        const float*, int, const float*, int, plalgo::OutBeat*, pinrec::SlotBeat*, int, int, float, float);

int main(int argc, char** argv) {
    const int hazard = argc > 1 ? std::atoi(argv[1]) : pinrec::HAZARD_DISTANCE;
    fixture::SyntheticSpec spec;
    spec.cells = 400; spec.macros = 3; spec.fixed = 12; spec.nets = 380;
    spec.max_large_degree = pinrec::MAX_LARGE_NET_DEGREE;
    packer::Netlist nl = fixture::build_synthetic(20260923u, spec);
    for (int pins : {64, 40}) {
        const int cell = pins == 64 ? 50 : 52;
        std::vector<packer::Pin> split(pins, packer::Pin{cell, {0, 0}});
        split.push_back({cell + 1, {0, 0}});
        nl.nets.push_back(split);
    }
    packer::Config cfg;
    cfg.hazard = hazard;
    cfg.large_nets = true;
    const packer::Encoded enc = packer::encode_netlist(nl, cfg);
    if (packer::check(nl, enc, cfg)) { printf("FAIL packer check\n"); return 1; }
    long max_extent = 0;
    {
        long first_real = -1;
        for (size_t position = (size_t)enc.beat_count.back(); position < enc.issue.size(); position++) {
            const int b = enc.issue[position];
            if (b == packer::BUBBLE) continue;
            if (first_real < 0) first_real = (long)position;
            if (enc.beats[b].last_of_net) { max_extent = std::max(max_extent, (long)position - first_real + 1); first_real = -1; }
        }
    }

    // How tight is this schedule? Count node updates closer than HAZARD_DISTANCE beats apart. Meow.
    long tight = 0;
    {
        std::vector<long> last(enc.work.movable.size(), -1000);
        for (size_t position = 0; position < enc.issue.size(); position++) {
            if (enc.issue[position] == packer::BUBBLE) continue;
            const packer::Beat& beat = enc.beats[enc.issue[position]];
            std::vector<int> nodes;   // a large-net beat holds only its own pins of the net
            for (int net : beat.nets) {
                if (beat.span > 1) for (int pin : beat.pins) nodes.push_back(enc.work.nets[net][pin].node);
                else               nodes.insert(nodes.end(), enc.unique_nodes[net].begin(), enc.unique_nodes[net].end());
            }
            std::sort(nodes.begin(), nodes.end());
            nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
            for (int node : nodes) {
                if (!enc.work.movable[node]) continue;
                tight += (long)position - last[node] < pinrec::HAZARD_DISTANCE;
                last[node] = (long)position;
            }
        }
    }

    // Co-simulation copies `depth` elements from every m_axi pointer (cosim_top.cpp), so every
    // buffer is padded up to its port's depth. Meow.
    auto at_least = [](size_t n, size_t depth) { return n > depth ? n : depth; };
    golden::Lut lut = golden::make_lut();
    std::vector<float> lut_table = lut.table;
    lut_table.resize(at_least(lut_table.size(), 1024), 0.0f);
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u, golden::EXTENT),
                                            fixture::random_positions(nl.movable.size(), 12u, golden::EXTENT)};
    bool ok = true;
    for (int axis = 0; axis < 2; axis++) {
        const long num_beats = (long)enc.issue.size();
        std::vector<pinrec::RecordBeat> records(at_least(num_beats, 1024));
        std::memcpy(records.data(), enc.records[axis].data(), num_beats * sizeof(pinrec::RecordBeat));
        const std::vector<float> image = packer::slot_positions(enc, axis, node_pos[axis]);
        const long num_slot_beats = enc.num_slots / pinrec::LANES;
        std::vector<pinrec::SlotBeat> pos(at_least(num_slot_beats, 512));
        std::memcpy(pos.data(), image.data(), image.size() * sizeof(float));
        std::vector<pinrec::MacroPinRef> refs = packer::macro_pin_refs(enc, axis);
        const long num_refs = (long)refs.size();
        refs.resize(at_least(refs.size(), 2048));
        std::vector<float> offsets = enc.offset_table[axis];
        const long num_offsets = (long)offsets.size();
        offsets.resize(at_least(offsets.size(), 1024), 0.0f);
        std::vector<pinrec::SlotBeat> grad(at_least(enc.first_fixed_slot / pinrec::LANES, 512));
        std::vector<plalgo::OutBeat> out(at_least(num_beats, 1024));
        std::vector<int> beat_count = enc.beat_count;
        std::vector<int> span_count = enc.span_beat_count;
        hpwl_gradient_computer_top(records.data(), (int)num_beats, beat_count.data(), span_count.data(),
                                   pos.data(), (int)num_slot_beats,
                                   refs.data(), (int)num_refs, offsets.data(), (int)num_offsets,
                                   lut_table.data(), lut.size, out.data(), grad.data(),
                                   (int)enc.first_fixed_slot, enc.offset_bits, 1.0f / golden::GAMMA, lut.inv_step);
        const float* grad_slots = reinterpret_cast<const float*>(grad.data());
        std::vector<float> node_grad(nl.movable.size(), 0.0f);
        for (size_t node = 0; node < nl.movable.size(); node++)
            if (enc.node_slot[node] >= 0 && enc.node_slot[node] < enc.first_fixed_slot) node_grad[node] = grad_slots[enc.node_slot[node]];
        const golden::Err s = golden::compare(nl, node_grad, golden::wa_gradient(nl, axis, node_pos[axis], lut, true, enc.large_nets));
        const bool s_ok = s.rel_rms < golden::STRUCT_RMS_TOL && s.max_rel < golden::STRUCT_MAX_TOL;
        printf("%s axis %d: rel_rms=%.3e max_rel=%.3e\n", s_ok ? "ok  " : "FAIL", axis, s.rel_rms, s.max_rel);
        ok &= s_ok;
    }
    printf("%s: hazard=%d, %zu beats, %ld node updates closer than %d beats, %zu large nets, %ld pads, max extent %ld\n",
           ok ? "PASS" : "FAIL", hazard, enc.issue.size(), tight, pinrec::HAZARD_DISTANCE, enc.large_nets.size(),
           enc.large_pads, max_extent);
    return ok ? 0 : 1;
}
