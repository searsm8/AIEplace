// cosim_tb.cpp -- RTL co-simulation bench for hpwl_gradient_CU_top: the chunked send / compute /
// fold passes, the mailbox and the external-gradient collect, as synthesized RTL. A small synthetic
// design is split into chunks by a small capacity. Same golden and tolerance as tier 1. Exits 0 on
// pass.
//
//   cosim_tb [CAPACITY [HAZARD]]    defaults 160, HAZARD_DISTANCE
//
// The mailbox loops skip padding segments whole (#42 option A), so across a skipped segment the
// collect read-add-write is spaced only by the pipeline draining between two invocations of
// collect_parcel. At the default hazard the run must contain such a case -- a slot whose entries
// straddle a padding segment closer than HAZARD_DISTANCE -- or it proves nothing. HAZARD < 4 is
// the negative control: the packer then places same-slot updates too close, and co-sim must FAIL.
// From vck5000/bring_up/hpwl_gradient_computer_v2/cosim/cosim_tb.cpp @ 41f2193 (#45). Meow.

#include "wa_gradient_golden.hpp"
#include "modules/hpwl_gradient_CU.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void hpwl_gradient_CU_top(
        const pinrec::ChunkDesc*, const int32_t*, int, const pinrec::RecordBeat*, const pinrec::SlotBeat*, const pinrec::MacroPinRef*,
        const int32_t*, const int32_t*, const pinrec::ParcelRef*, float*, const float*, int, const float*, int,
        pinrec::SlotBeat*, int, float, float, int64_t*);

// Co-simulation copies `depth` elements from every pointer (cosim_top.cpp): pad to it. Meow.
template <class T> static std::vector<T> padded(std::vector<T> v, size_t depth) {
    if (v.size() < depth) v.resize(depth);
    return v;
}

int main(int argc, char** argv) {
    const long capacity = argc > 1 ? std::atol(argv[1]) : 160;
    packer::Config cfg;
    if (argc > 2) cfg.hazard = std::atoi(argv[2]);
    const bool negative_control = cfg.hazard < pinrec::HAZARD_DISTANCE;
    fixture::SyntheticSpec spec;
    spec.cells = 400; spec.macros = 3; spec.fixed = 12; spec.nets = 380;
    const packer::Netlist nl = fixture::build_synthetic(20260923u, spec);
    const packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
    if (packer::check_chunked(nl, ch, cfg)) { printf("FAIL chunked packer check\n"); return 1; }

    const std::vector<int> large_nets = packer::chunked_large_nets(ch);
    long padding_segments = 0;
    for (const packer::Chunk& c : ch.chunks)
        for (const auto& parcel : c.parcels) padding_segments += parcel.offset < 0;
    const long straddling = fixture::straddling_pairs(ch);
    printf("     capacity=%ld hazard=%d: %d chunks, %ld external, %zu large nets, %ld padding segments, %ld straddling pairs\n",
           capacity, cfg.hazard, ch.num_chunks, ch.externals, large_nets.size(), padding_segments, straddling);
    if (!negative_control && (ch.num_chunks < 2 || straddling == 0)) {
        printf("FAIL coverage: no collect entries straddle a padding segment, so the skip is untested\n");
        return 1;
    }

    const golden::Lut lut = golden::make_lut();
    const std::vector<float> lut_table = padded(lut.table, 1024);
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u, golden::EXTENT),
                                            fixture::random_positions(nl.movable.size(), 12u, golden::EXTENT)};
    bool ok = true;
    for (int axis = 0; axis < 2; axis++) {
        const packer::ChunkedDevice dev = packer::chunked_device_arrays(ch, axis, node_pos[axis]);
        const bool fits = dev.records.size() / pinrec::LANES <= 1024 && dev.slot_images.size() / pinrec::LANES <= 1024 &&
                          dev.desc.size() <= 16 && dev.group_ends.size() <= 1024 && dev.macro_pins.size() <= 2048 &&
                          dev.external_slots.size() <= 4096 && dev.shared_slots.size() <= 4096 && dev.parcels.size() <= 256 &&
                          dev.mailbox_size <= 4096;
        if (!fits) { printf("FAIL design exceeds a cosim port depth\n"); return 1; }
        std::vector<pinrec::RecordBeat> records(1024);
        std::memcpy(records.data(), dev.records.data(), dev.records.size() * sizeof(uint32_t));
        std::vector<pinrec::SlotBeat> pos(1024);
        std::memcpy(pos.data(), dev.slot_images.data(), dev.slot_images.size() * sizeof(float));
        const std::vector<pinrec::ChunkDesc> desc = padded(dev.desc, 16);
        const std::vector<int32_t> group_ends = padded(dev.group_ends, 1024);
        const std::vector<pinrec::MacroPinRef> refs = padded(dev.macro_pins, 2048);
        const std::vector<int32_t> external_slots = padded(dev.external_slots, 4096), shared_slots = padded(dev.shared_slots, 4096);
        const std::vector<pinrec::ParcelRef> parcels = padded(dev.parcels, 256);
        const std::vector<float> offsets = padded(dev.offset_table, 1024);
        std::vector<float> mailbox(4096, NAN);
        std::vector<pinrec::SlotBeat> grad(1024);
        int64_t hpwl_total_fixed = -1;
        hpwl_gradient_CU_top(desc.data(), group_ends.data(), (int)dev.desc.size(), records.data(), pos.data(), refs.data(),
                             external_slots.data(), shared_slots.data(), parcels.data(), mailbox.data(), offsets.data(),
                             (int)dev.offset_table.size(), lut_table.data(), lut.size, grad.data(),
                             dev.offset_bits, 1.0f / golden::GAMMA, lut.inv_step, &hpwl_total_fixed);

        const float* grad_slots = reinterpret_cast<const float*>(grad.data());
        std::vector<float> node_grad(nl.movable.size(), NAN);
        for (int k = 0; k < ch.num_chunks; k++) {
            const packer::Chunk& c = ch.chunks[k];
            const long base = (long)dev.desc[k].slot_beat_offset * pinrec::LANES;
            for (size_t l = 0; l < c.work_node.size(); l++) {
                const int node = c.work_node[l];
                if (!c.external[l] && (size_t)node < nl.movable.size() && ch.global.work.movable[node])
                    node_grad[node] = grad_slots[base + c.enc.node_slot[l]];
            }
        }
        const golden::Err s = golden::compare(nl, node_grad, golden::wa_gradient(nl, axis, node_pos[axis], lut, true, large_nets));
        const bool s_ok = s.rel_rms < golden::STRUCT_RMS_TOL && s.max_rel < golden::STRUCT_MAX_TOL;
        printf("%s axis %d: rel_rms=%.3e max_rel=%.3e\n", s_ok ? "ok  " : "FAIL", axis, s.rel_rms, s.max_rel);
        ok &= s_ok;
        const int64_t hpwl_want = golden::hpwl_total_fixed(nl, axis, node_pos[axis], large_nets);
        printf("%s axis %d: HPWL total device=%lld golden=%lld\n", hpwl_total_fixed == hpwl_want ? "ok  " : "FAIL", axis,
               (long long)hpwl_total_fixed, (long long)hpwl_want);
        ok &= hpwl_total_fixed == hpwl_want;
    }
    printf("%s: hpwl_gradient_CU co-sim, capacity=%ld hazard=%d\n", ok ? "PASS" : "FAIL", capacity, cfg.hazard);
    return ok ? 0 : 1;
}
