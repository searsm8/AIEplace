// Verify hpwl_gradient_computer_v2 (bring_up/hpwl_gradient_computer_v2) -- WA gradient for a
// design split into chunks: ghost positions out, ghost gradients back, through DDR (#41).
//
// Golden: wa_gradient_golden.hpp (sw_only's WA partial in double from the PARSED netlist), so it
// is independent of the partition, homes, ghosts and exchange layout. Each movable parsed node's
// device gradient is read from its OWNER chunk's slot. Ghost slots, macro-pin slots and the
// exchange buffer all start as NaN, so a missed import, export or fold cannot pass.
//
//   [1] STRUCTURE  vs same-LUT golden, rel_rms < 1e-5, max_rel < 1e-4, both axes:
//                  capacity 2048 (several chunks) and capacity 1M (one chunk, no exchange)
//   [2] LUT BUDGET vs true-exp golden, rel_rms < 1e-4, max_rel < 1e-3 (chunked config)
//   [3] ZERO       movable nodes with no in-scope pin get exactly 0
//   [4] HPWL       the per-net HPWL by-product, bit-exact
//   [5] COVERAGE   several chunks; ghost cells and ghost macro pins exist; ghost gradients carry
//                  a real share of the total (a dropped return path would be visible)
//
// `hpwl_gradient_computer_v2_test --bookshelf DIR NAME [CAPACITY]` / `--def FILE NAME [CAPACITY]`
// runs [1]+[4] on a real design; CAPACITY defaults to the on-chip SLOT_CAPACITY. Meow.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "wa_gradient_golden.hpp"
#include "modules/hpwl_gradient_computer_v2.hpp"

#include <cstdio>

using namespace plalgo;

struct DeviceAxis {
    std::vector<OutBeat> out;
    std::vector<float>   node_grad;   // per parsed node
};

static DeviceAxis run_axis(const packer::Chunked& ch, size_t num_parsed, int axis, const std::vector<float>& node_pos,
                           const golden::Lut& lut) {
    const packer::ChunkedDevice dev = packer::chunked_device_arrays(ch, axis, node_pos);
    std::vector<pinrec::RecordBeat> records(dev.records.size() / pinrec::LANES);
    std::memcpy(records.data(), dev.records.data(), dev.records.size() * sizeof(uint32_t));
    std::vector<pinrec::SlotBeat> pos(dev.slot_images.size() / pinrec::LANES);
    std::memcpy(pos.data(), dev.slot_images.data(), dev.slot_images.size() * sizeof(float));
    std::vector<pinrec::SlotBeat> grad(pos.size());
    for (auto& beat : grad) for (float& v : beat.v) v = NAN;     // unwritten slots stay visibly wrong
    std::vector<float> exchange(dev.exchange_size + 1, NAN);
    DeviceAxis out;
    out.out.resize(records.size());
    hpwl_gradient_computer_v2(dev.desc.data(), (int)dev.desc.size(), records.data(), pos.data(), dev.macro_pins.data(),
                              dev.import_slots.data(), dev.export_slots.data(), dev.blocks.data(), exchange.data(),
                              dev.offset_table.data(), (int)dev.offset_table.size(), lut.table.data(), lut.size,
                              out.out.data(), grad.data(), dev.offset_bits, 1.0f / golden::GAMMA, lut.inv_step);

    const float* grad_slots = reinterpret_cast<const float*>(grad.data());
    out.node_grad.assign(num_parsed, NAN);
    for (int k = 0; k < ch.num_chunks; k++) {
        const packer::Chunk& c = ch.chunks[k];
        const long base = (long)dev.desc[k].slot_beat_offset * pinrec::LANES;
        for (size_t l = 0; l < c.work_node.size(); l++) {
            const int node = c.work_node[l];
            if (!c.ghost[l] && (size_t)node < num_parsed && ch.global.work.movable[node])
                out.node_grad[node] = grad_slots[base + c.enc.node_slot[l]];
        }
    }
    return out;
}

static bool run_config(const char* label, const packer::Netlist& nl, long capacity, const std::vector<float> node_pos[2],
                       const golden::Lut& lut, bool check_lut_budget, packer::Chunked* keep = nullptr) {
    const packer::Config cfg;
    packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
    if (packer::check_chunked(nl, ch, cfg)) { printf("FAIL [%s] chunked packer check\n", label); return false; }
    for (const auto& c : ch.chunks)
        if (c.enc.num_slots > SLOT_CAPACITY) { printf("FAIL [%s] a chunk exceeds on-chip capacity\n", label); return false; }
    std::vector<char> touched(nl.movable.size(), 0);
    for (const auto& net : nl.nets) if (packer::in_scope(net)) for (const auto& pin : net) touched[pin.node] = 1;

    bool ok = true;
    printf("     [%s] %d chunks, %ld ghosts\n", label, ch.num_chunks, ch.ghosts);
    for (int axis = 0; axis < 2; axis++) {
        const DeviceAxis dev = run_axis(ch, nl.movable.size(), axis, node_pos[axis], lut);
        const golden::Err s = golden::compare(nl, dev.node_grad, golden::wa_gradient(nl, axis, node_pos[axis], lut, true));
        const bool s_ok = s.rel_rms < golden::STRUCT_RMS_TOL && s.max_rel < golden::STRUCT_MAX_TOL;
        printf("%s [1] [%s] axis %d structure: rel_rms=%.3e max_rel=%.3e\n", s_ok ? "ok  " : "FAIL", label, axis, s.rel_rms, s.max_rel);
        ok &= s_ok;
        if (check_lut_budget) {
            const golden::Err l = golden::compare(nl, dev.node_grad, golden::wa_gradient(nl, axis, node_pos[axis], lut, false));
            const bool l_ok = l.rel_rms < golden::LUT_RMS_TOL && l.max_rel < golden::LUT_MAX_TOL;
            printf("%s [2] [%s] axis %d LUT budget: rel_rms=%.3e max_rel=%.3e\n", l_ok ? "ok  " : "FAIL", label, axis, l.rel_rms, l.max_rel);
            ok &= l_ok;
        }
        long hpwl_bad = 0, base = 0;
        for (const packer::Chunk& c : ch.chunks) {
            for (size_t position = 0; position < c.enc.issue.size(); position++)
                for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
                    const int local = c.enc.net_at[position * pinrec::MAX_NETS_PER_BEAT + k];
                    const float want = local == packer::NO_NET ? 0.0f : golden::span(nl, c.net_global[local], axis, node_pos[axis]);
                    hpwl_bad += !(dev.out[base + position].v[k] == want);
                }
            base += (long)c.enc.issue.size();
        }
        printf("%s [4] [%s] axis %d HPWL by-product: %ld mismatches\n", hpwl_bad ? "FAIL" : "ok  ", label, axis, hpwl_bad);
        ok &= hpwl_bad == 0;

        long untouched = 0, nonzero = 0;
        for (size_t node = 0; node < nl.movable.size(); node++)
            if (nl.movable[node] && !touched[node]) { untouched++; nonzero += dev.node_grad[node] != 0.0f; }
        if (nonzero) { printf("FAIL [3] [%s] axis %d: %ld of %ld untouched nodes non-zero\n", label, axis, nonzero, untouched); ok = false; }
    }
    if (keep) *keep = std::move(ch);
    return ok;
}

int main(int argc, char** argv) {
    packer::Netlist nl;
    const bool real = argc >= 4 && fixture::load_from_args(4, argv, nl);
    if (!real) nl = fixture::build_synthetic(20260922u);
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u, golden::EXTENT),
                                            fixture::random_positions(nl.movable.size(), 12u, golden::EXTENT)};
    const golden::Lut lut = golden::make_lut();
    if (real) {
        const long capacity = argc >= 5 ? std::atol(argv[4]) : SLOT_CAPACITY;
        return run_config(nl.name.c_str(), nl, capacity, node_pos, lut, false) ? 0 : 1;
    }

    bool ok = true;
    packer::Chunked ch;
    ok &= run_config("capacity=2048", nl, 2048, node_pos, lut, true, &ch);
    ok &= run_config("capacity=1M", nl, SLOT_CAPACITY, node_pos, lut, false);

    // [5] coverage: the return path must carry real gradient. Share of |grad|^2 on nodes that are
    // ghosts somewhere (their total needs the exchange) -- a dropped return path would lose it.
    long ghost_cells = 0, ghost_macro_pins = 0;
    std::vector<char> ghosted(ch.global.kind.size(), 0);
    for (const packer::Chunk& c : ch.chunks)
        for (size_t l = 0; l < c.work_node.size(); l++)
            if (c.ghost[l]) {
                ghosted[c.work_node[l]] = 1;
                ghost_cells += ch.global.kind[c.work_node[l]] == packer::CELL;
                ghost_macro_pins += ch.global.kind[c.work_node[l]] == packer::MACRO_PIN;
            }
    const std::vector<double> gx = golden::wa_gradient(nl, 0, node_pos[0], lut, true);
    double ghosted_share = 0, total = 0;
    for (size_t node = 0; node < nl.movable.size(); node++) {
        if (!nl.movable[node]) continue;
        total += gx[node] * gx[node];
        if (ghosted[node]) ghosted_share += gx[node] * gx[node];
    }
    const bool covered = ch.num_chunks >= 3 && ghost_cells >= 100 && ghost_macro_pins >= 10 && ghosted_share > 0.05 * total;
    printf("%s [5] coverage: %d chunks, %ld cell ghosts, %ld macro-pin ghosts, ghosted cells carry %.1f%% of |grad|^2\n",
           covered ? "ok  " : "FAIL", ch.num_chunks, ghost_cells, ghost_macro_pins, 100.0 * ghosted_share / total);
    ok &= covered;

    printf(ok ? "PASS: hpwl_gradient_computer_v2\n" : "FAIL: hpwl_gradient_computer_v2\n");
    return ok ? 0 : 1;
}
