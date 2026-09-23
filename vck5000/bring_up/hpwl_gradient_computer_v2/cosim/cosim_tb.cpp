// cosim_tb.cpp -- RTL co-simulation test bench for hpwl_gradient_computer_v2_top (#41): the chunked
// gradient's export / compute / fold passes, the exchange buffer and the ghost-gradient add, run
// as synthesized RTL. A small synthetic design is split into several chunks by a small capacity
// (argv[1], default 256 slots). Same golden and tolerance as tier 1. Exits 0 on pass. Meow.

#include "wa_gradient_golden.hpp"
#include "modules/hpwl_gradient_computer_v2.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void hpwl_gradient_computer_v2_top(
        const pinrec::ChunkDesc*, int, const pinrec::RecordBeat*, const pinrec::SlotBeat*, const pinrec::MacroPinRef*,
        const int32_t*, const int32_t*, const pinrec::ExchangeBlockRef*, float*, const float*, int, const float*, int,
        plalgo::OutBeat*, pinrec::SlotBeat*, int, float, float);

// Co-simulation copies `depth` elements from every pointer (cosim_top.cpp): pad to it. Meow.
template <class T> static std::vector<T> padded(std::vector<T> v, size_t depth) {
    if (v.size() < depth) v.resize(depth);
    return v;
}

int main(int argc, char** argv) {
    const long capacity = argc > 1 ? std::atol(argv[1]) : 256;
    fixture::SyntheticSpec spec;
    spec.cells = 400; spec.macros = 3; spec.fixed = 12; spec.nets = 380;
    const packer::Netlist nl = fixture::build_synthetic(20260923u, spec);
    const packer::Config cfg;
    const packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
    if (packer::check_chunked(nl, ch, cfg)) { printf("FAIL chunked packer check\n"); return 1; }

    const golden::Lut lut = golden::make_lut();
    const std::vector<float> lut_table = padded(lut.table, 1024);
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u, golden::EXTENT),
                                            fixture::random_positions(nl.movable.size(), 12u, golden::EXTENT)};
    bool ok = true;
    for (int axis = 0; axis < 2; axis++) {
        const packer::ChunkedDevice dev = packer::chunked_device_arrays(ch, axis, node_pos[axis]);
        std::vector<pinrec::RecordBeat> records(std::max<size_t>(dev.records.size() / pinrec::LANES, 1024));
        std::memcpy(records.data(), dev.records.data(), dev.records.size() * sizeof(uint32_t));
        std::vector<pinrec::SlotBeat> pos(std::max<size_t>(dev.slot_images.size() / pinrec::LANES, 1024));
        std::memcpy(pos.data(), dev.slot_images.data(), dev.slot_images.size() * sizeof(float));
        const std::vector<pinrec::ChunkDesc> desc = padded(dev.desc, 16);
        const std::vector<pinrec::MacroPinRef> refs = padded(dev.macro_pins, 2048);
        const std::vector<int32_t> imports = padded(dev.import_slots, 4096), exports = padded(dev.export_slots, 4096);
        const std::vector<pinrec::ExchangeBlockRef> blocks = padded(dev.blocks, 256);
        const std::vector<float> offsets = padded(dev.offset_table, 1024);
        std::vector<float> exchange(4096, NAN);
        std::vector<plalgo::OutBeat> out(1024);
        std::vector<pinrec::SlotBeat> grad(1024);
        hpwl_gradient_computer_v2_top(desc.data(), (int)dev.desc.size(), records.data(), pos.data(), refs.data(),
                                      imports.data(), exports.data(), blocks.data(), exchange.data(), offsets.data(),
                                      (int)dev.offset_table.size(), lut_table.data(), lut.size, out.data(), grad.data(),
                                      dev.offset_bits, 1.0f / golden::GAMMA, lut.inv_step);

        const float* grad_slots = reinterpret_cast<const float*>(grad.data());
        std::vector<float> node_grad(nl.movable.size(), NAN);
        for (int k = 0; k < ch.num_chunks; k++) {
            const packer::Chunk& c = ch.chunks[k];
            const long base = (long)dev.desc[k].slot_beat_offset * pinrec::LANES;
            for (size_t l = 0; l < c.work_node.size(); l++) {
                const int node = c.work_node[l];
                if (!c.ghost[l] && (size_t)node < nl.movable.size() && ch.global.work.movable[node])
                    node_grad[node] = grad_slots[base + c.enc.node_slot[l]];
            }
        }
        const golden::Err s = golden::compare(nl, node_grad, golden::wa_gradient(nl, axis, node_pos[axis], lut, true));
        const bool s_ok = s.rel_rms < golden::STRUCT_RMS_TOL && s.max_rel < golden::STRUCT_MAX_TOL;
        printf("%s axis %d: rel_rms=%.3e max_rel=%.3e\n", s_ok ? "ok  " : "FAIL", axis, s.rel_rms, s.max_rel);
        ok &= s_ok;
        if (axis == 0) {
            const bool fits = dev.records.size() / pinrec::LANES <= 1024 && dev.slot_images.size() / pinrec::LANES <= 1024 &&
                              dev.desc.size() <= 16 && dev.macro_pins.size() <= 2048 && dev.import_slots.size() <= 4096 &&
                              dev.export_slots.size() <= 4096 && dev.blocks.size() <= 256 && dev.exchange_size <= 4096;
            if (!fits) { printf("FAIL design exceeds a cosim port depth\n"); return 1; }
        }
    }
    printf("%s: capacity=%ld, %d chunks, %ld ghosts\n", ok ? "PASS" : "FAIL", capacity, ch.num_chunks, ch.ghosts);
    return ok ? 0 : 1;
}
