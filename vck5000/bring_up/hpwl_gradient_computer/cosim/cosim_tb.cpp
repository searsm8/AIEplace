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
// Same golden and tolerance as tier 1 (wa_gradient_golden.hpp). Exits 0 on pass. Meow.

#include "wa_gradient_golden.hpp"
#include "modules/hpwl_gradient_computer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void hpwl_gradient_computer_top(
        const pinrec::RecordBeat*, int, const int*, const pinrec::SlotBeat*, int, const pinrec::MacroPinRef*, int,
        const float*, int, const float*, int, plalgo::OutBeat*, pinrec::SlotBeat*, int, int, float, float);

int main(int argc, char** argv) {
    const int hazard = argc > 1 ? std::atoi(argv[1]) : pinrec::HAZARD_DISTANCE;
    fixture::SyntheticSpec spec;
    spec.cells = 400; spec.macros = 3; spec.fixed = 12; spec.nets = 380;
    const packer::Netlist nl = fixture::build_synthetic(20260923u, spec);
    packer::Config cfg;
    cfg.hazard = hazard;
    const packer::Encoded enc = packer::encode_netlist(nl, cfg);
    if (packer::check(nl, enc, cfg)) { printf("FAIL packer check\n"); return 1; }

    // How tight is this schedule? Count node updates closer than HAZARD_DISTANCE beats apart. Meow.
    long tight = 0;
    {
        std::vector<long> last(enc.work.movable.size(), -1000);
        for (size_t position = 0; position < enc.issue.size(); position++) {
            if (enc.issue[position] == packer::BUBBLE) continue;
            for (int net : enc.beats[enc.issue[position]].nets)
                for (int node : enc.unique_nodes[net]) {
                    if (!enc.work.movable[node]) continue;
                    tight += (long)position - last[node] < pinrec::HAZARD_DISTANCE;
                    last[node] = (long)position;
                }
        }
    }

    const golden::Lut lut = golden::make_lut();
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u, golden::EXTENT),
                                            fixture::random_positions(nl.movable.size(), 12u, golden::EXTENT)};
    bool ok = true;
    for (int axis = 0; axis < 2; axis++) {
        const long num_beats = (long)enc.issue.size();
        std::vector<pinrec::RecordBeat> records(num_beats);
        std::memcpy(records.data(), enc.records[axis].data(), num_beats * sizeof(pinrec::RecordBeat));
        const std::vector<float> image = packer::slot_positions(enc, axis, node_pos[axis]);
        std::vector<pinrec::SlotBeat> pos(enc.num_slots / pinrec::LANES);
        std::memcpy(pos.data(), image.data(), image.size() * sizeof(float));
        const std::vector<pinrec::MacroPinRef> refs = packer::macro_pin_refs(enc, axis);
        std::vector<pinrec::SlotBeat> grad(enc.first_fixed_slot / pinrec::LANES);
        std::vector<plalgo::OutBeat> out(num_beats);
        hpwl_gradient_computer_top(records.data(), (int)num_beats, enc.beat_count.data(), pos.data(), (int)pos.size(),
                                   refs.data(), (int)refs.size(), enc.offset_table[axis].data(),
                                   (int)enc.offset_table[axis].size(), lut.table.data(), lut.size, out.data(), grad.data(),
                                   (int)enc.first_fixed_slot, enc.offset_bits, 1.0f / golden::GAMMA, lut.inv_step);
        const float* grad_slots = reinterpret_cast<const float*>(grad.data());
        std::vector<float> node_grad(nl.movable.size(), 0.0f);
        for (size_t node = 0; node < nl.movable.size(); node++)
            if (enc.node_slot[node] >= 0 && enc.node_slot[node] < enc.first_fixed_slot) node_grad[node] = grad_slots[enc.node_slot[node]];
        const golden::Err s = golden::compare(nl, node_grad, golden::wa_gradient(nl, axis, node_pos[axis], lut, true));
        const bool s_ok = s.rel_rms < golden::STRUCT_RMS_TOL && s.max_rel < golden::STRUCT_MAX_TOL;
        printf("%s axis %d: rel_rms=%.3e max_rel=%.3e\n", s_ok ? "ok  " : "FAIL", axis, s.rel_rms, s.max_rel);
        ok &= s_ok;
    }
    printf("%s: hazard=%d, %zu beats, %ld node updates closer than %d beats\n", ok ? "PASS" : "FAIL",
           hazard, enc.issue.size(), tight, pinrec::HAZARD_DISTANCE);
    return ok ? 0 : 1;
}
