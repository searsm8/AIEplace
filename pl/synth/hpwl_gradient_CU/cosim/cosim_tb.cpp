// cosim_tb.cpp -- RTL co-simulation bench for hpwl_gradient_CU_top: the chunked compute pass and the
// gradient scatter-add, as synthesized RTL. A small synthetic design is split into chunks by a small
// capacity; the borrowed exchange around the CU is iteration_update_CU's (#47 B4 = A), here its golden
// (borrowed_fusion.hpp fused_send before, fused_merge after). Same golden and tolerance as tier 1.
// Exits 0 on pass.
//
//   cosim_tb [CAPACITY [HAZARD]]    defaults 1536 (16 chunks; 1280 and below fit no K, 2048 is one chunk), HAZARD_DISTANCE
//
// HAZARD < 4 is the negative control for the beat loop's scatter-add: the packer then places
// same-slot updates too close, and co-sim must FAIL.
// From vck5000/bring_up/hpwl_gradient_computer_v2/cosim/cosim_tb.cpp @ 41f2193 (#45). Meow.

#include "wa_gradient_golden.hpp"
#include "modules/hpwl_gradient_CU.hpp"
#include "borrowed_fusion.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void hpwl_gradient_CU_top(
        const pinrec::ChunkDesc*, const int32_t*, int, const pinrec::RecordBeat*, const pinrec::SlotBeat*, const pinrec::MacroBeat*, const pinrec::SlotBeat*,
        const float*, int, const float*, int,
        pinrec::SlotBeat*, int, float, float, int64_t*);

// Co-simulation copies `depth` elements from every pointer (cosim_top.cpp): pad to it. Meow.
template <class T> static std::vector<T> padded(std::vector<T> v, size_t depth) {
    if (v.size() < depth) v.resize(depth);
    return v;
}

int main(int argc, char** argv) {
    const long capacity = argc > 1 ? std::atol(argv[1]) : 1536;
    packer::Config cfg;
    if (argc > 2) cfg.hazard = std::atoi(argv[2]);
    const bool negative_control = cfg.hazard < pinrec::HAZARD_DISTANCE;
    fixture::SyntheticSpec spec;
    spec.cells = 1200; spec.macros = 5; spec.fixed = 20; spec.nets = 1140;
    const packer::Netlist nl = fixture::build_synthetic(20260923u, spec);
    const packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
    if (packer::check_chunked(nl, ch, cfg)) { printf("FAIL chunked packer check\n"); return 1; }

    const std::vector<int> large_nets = packer::chunked_large_nets(ch);
    printf("     capacity=%ld hazard=%d: %d chunks, %ld borrowed, %zu large nets\n",
           capacity, cfg.hazard, ch.num_chunks, ch.num_borrowed, large_nets.size());
    if (!negative_control && (ch.num_chunks < 3 || ch.num_borrowed == 0)) {
        printf("FAIL coverage: the run lacks several chunks or a borrowed node\n");
        return 1;
    }

    const golden::Lut lut = golden::make_lut();
    const std::vector<float> lut_table = padded(lut.table, 1024);
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u, golden::EXTENT),
                                            fixture::random_positions(nl.movable.size(), 12u, golden::EXTENT)};
    const std::vector<std::vector<sw_flat::BorrowedParcel>> parcels = sw_flat::borrowed_parcels(ch);
    bool ok = true;
    for (int axis = 0; axis < 2; axis++) {
        const packer::ChunkedDevice dev = packer::chunked_device_arrays(ch, axis, node_pos[axis]);
        const bool fits = dev.records.size() / pinrec::LANES <= 2048 && dev.slot_images.size() / pinrec::LANES <= 4096 &&
                          dev.desc.size() <= 64 && dev.group_ends.size() <= 2048 && dev.macro_beats.size() <= 4096;
        if (!fits) { printf("FAIL design exceeds a cosim port depth\n"); return 1; }
        std::vector<pinrec::RecordBeat> records(2048);
        std::memcpy(records.data(), dev.records.data(), dev.records.size() * sizeof(uint32_t));
        std::vector<float> pos_slots = dev.slot_images;
        for (int j = 0; j < ch.num_chunks; j++) sw_flat::fused_send(ch, j, parcels[j], pos_slots);   // iteration_update_CU's STEP, before
        std::vector<pinrec::SlotBeat> pos(4096);
        std::memcpy(pos.data(), pos_slots.data(), pos_slots.size() * sizeof(float));
        const std::vector<pinrec::ChunkDesc> desc = padded(dev.desc, 64);
        const std::vector<int32_t> group_ends = padded(dev.group_ends, 2048);
        const std::vector<pinrec::MacroBeat> macro_beats = padded(dev.macro_beats, 4096);
        const std::vector<pinrec::SlotBeat> macro_offsets = padded(dev.macro_offsets, 4096);
        const std::vector<float> offsets = padded(dev.offset_table, 1024);
        std::vector<pinrec::SlotBeat> grad(4096);
        int64_t hpwl_total_fixed = -1;
        hpwl_gradient_CU_top(desc.data(), group_ends.data(), (int)dev.desc.size(), records.data(), pos.data(), macro_beats.data(), macro_offsets.data(),
                             offsets.data(), (int)dev.offset_table.size(), lut_table.data(),
                             lut.size, grad.data(), dev.offset_bits, 1.0f / golden::GAMMA, lut.inv_step, &hpwl_total_fixed);

        std::vector<float> grad_slots(grad.size() * pinrec::LANES);
        std::memcpy(grad_slots.data(), grad.data(), grad_slots.size() * sizeof(float));
        for (int j = 0; j < ch.num_chunks; j++) sw_flat::fused_merge(ch, j, parcels[j], grad_slots);   // iteration_update_CU's COMBINE, after
        std::vector<float> node_grad(nl.movable.size(), NAN);
        for (int k = 0; k < ch.num_chunks; k++) {
            const packer::Chunk& c = ch.chunks[k];
            const long base = (long)dev.desc[k].slot_beat_offset * pinrec::LANES;
            for (size_t l = 0; l < c.work_node.size(); l++) {
                const int node = c.work_node[l];
                if (!c.borrowed[l] && (size_t)node < nl.movable.size() && ch.global.work.movable[node])
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
