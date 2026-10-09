// Verify hpwl_gradient_CU (src/modules/hpwl_gradient_CU.hpp) -- WA gradient for a design split into
// chunks: external positions out, external gradients back, through the consumers' tail rows (#41, #47).
// From vck5000/test/hpwl_gradient_computer_v2_test.cpp @ 41f2193 (#45). Meow.
//
// Golden: wa_gradient_golden.hpp (sw_only's WA partial in double from the PARSED netlist), so it
// is independent of the partition, homes, external slots and tail layout. Each movable parsed
// node's device gradient is read from its OWNER chunk's slot. External slots and macro-pin slots
// start as NaN, so a missed send, refresh or fold cannot pass.
//
//   [1] STRUCTURE  vs same-LUT golden, rel_rms < 1e-5, max_rel < 1e-3, both axes:
//                  capacity 4096 (several chunks), 2560 (bare cut, many owner blocks per tail) and
//                  1M (one chunk, no tail)
//   [2] LUT BUDGET vs true-exp golden, rel_rms < 1e-4, max_rel < 1e-3 (chunked config)
//   [3] ZERO       movable nodes with no in-scope pin get exactly 0
//   [4] HPWL       the total-HPWL by-product, bit-exact vs the golden's integer sum; the golden's
//                  quantization within 2^-HPWL_FRAC_BITS per net of the exact double sum
//   [5] COVERAGE   several chunks; external cells and macros exist; external gradients carry a
//                  real share of the total (a dropped merge would be visible); tails with many
//                  owner blocks, and blocks of several rows. Row-buffer wrap-around (a block of more
//                  than TAIL_BUFFER_ROWS rows) appears only on real designs (--netlist).
//
// `hpwl_gradient_CU_test --netlist FILE [CAPACITY]` runs [1]+[4] on a real design (FILE from
// `make -C host/sw_flat netlists`); CAPACITY defaults to the on-chip SLOT_CAPACITY. Meow.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "wa_gradient_golden.hpp"
#include "modules/hpwl_gradient_CU.hpp"

#include <cstdio>

using namespace plalgo;

struct DeviceAxis {
    int64_t hpwl_total_fixed = -1;
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
    DeviceAxis out;
    hpwl_gradient_CU(dev.desc.data(), dev.group_ends.data(), (int)dev.desc.size(), records.data(), pos.data(), dev.macro_pins.data(),
                     dev.streams.data(), dev.tail_ops.data(),
                     dev.offset_table.data(), (int)dev.offset_table.size(), lut.table.data(), lut.size,
                     grad.data(), dev.offset_bits, 1.0f / golden::GAMMA, lut.inv_step, out.hpwl_total_fixed);

    const float* grad_slots = reinterpret_cast<const float*>(grad.data());
    out.node_grad.assign(num_parsed, NAN);
    for (int k = 0; k < ch.num_chunks; k++) {
        const packer::Chunk& c = ch.chunks[k];
        const long base = (long)dev.desc[k].slot_beat_offset * pinrec::LANES;
        for (size_t l = 0; l < c.work_node.size(); l++) {
            const int node = c.work_node[l];
            if (!c.external[l] && (size_t)node < num_parsed && ch.global.work.movable[node])
                out.node_grad[node] = grad_slots[base + c.enc.node_slot[l]];
        }
    }
    return out;
}

static bool run_config(const char* label, const packer::Netlist& nl, long capacity, const std::vector<float> node_pos[2],
                       const golden::Lut& lut, bool check_lut_budget, packer::Chunked* keep = nullptr, int fm_passes = 1) {
    packer::Config cfg;
    cfg.partition_fm_passes = fm_passes;
    packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
    if (packer::check_chunked(nl, ch, cfg)) { printf("FAIL [%s] chunked packer check\n", label); return false; }
    for (const auto& c : ch.chunks)
        if (c.enc.num_slots > SLOT_CAPACITY) { printf("FAIL [%s] a chunk exceeds on-chip capacity\n", label); return false; }
    const std::vector<int> large_nets = packer::chunked_large_nets(ch);
    std::vector<char> touched(nl.movable.size(), 0);
    for (const auto& net : nl.nets) if (packer::in_scope(net)) for (const auto& pin : net) touched[pin.node] = 1;
    for (int net : large_nets) for (const auto& pin : nl.nets[net]) touched[pin.node] = 1;

    bool ok = true;
    printf("     [%s] %d chunks, %ld external, %zu large nets\n", label, ch.num_chunks, ch.externals, large_nets.size());
    for (int axis = 0; axis < 2; axis++) {
        const DeviceAxis dev = run_axis(ch, nl.movable.size(), axis, node_pos[axis], lut);
        const golden::Err s = golden::compare(nl, dev.node_grad, golden::wa_gradient(nl, axis, node_pos[axis], lut, true, large_nets));
        const bool s_ok = s.rel_rms < golden::STRUCT_RMS_TOL && s.max_rel < golden::STRUCT_MAX_TOL;
        printf("%s [1] [%s] axis %d structure: rel_rms=%.3e max_rel=%.3e\n", s_ok ? "ok  " : "FAIL", label, axis, s.rel_rms, s.max_rel);
        ok &= s_ok;
        if (check_lut_budget) {
            const golden::Err l = golden::compare(nl, dev.node_grad, golden::wa_gradient(nl, axis, node_pos[axis], lut, false, large_nets));
            const bool l_ok = l.rel_rms < golden::LUT_RMS_TOL && l.max_rel < golden::LUT_MAX_TOL;
            printf("%s [2] [%s] axis %d LUT budget: rel_rms=%.3e max_rel=%.3e\n", l_ok ? "ok  " : "FAIL", label, axis, l.rel_rms, l.max_rel);
            ok &= l_ok;
        }
        double hpwl_exact = 0;
        long hpwl_nets = 0;
        const int64_t hpwl_want = golden::hpwl_total_fixed(nl, axis, node_pos[axis], large_nets, &hpwl_exact, &hpwl_nets);
        const double hpwl_quant_err = std::fabs((double)hpwl_want / (1 << pinrec::HPWL_FRAC_BITS) - hpwl_exact);
        const bool hpwl_ok = dev.hpwl_total_fixed == hpwl_want && hpwl_quant_err <= hpwl_nets * std::ldexp(1.0, -pinrec::HPWL_FRAC_BITS);
        printf("%s [4] [%s] axis %d HPWL total: device=%lld golden=%lld (%ld nets, quantization %.2e)\n", hpwl_ok ? "ok  " : "FAIL",
               label, axis, (long long)dev.hpwl_total_fixed, (long long)hpwl_want, hpwl_nets, hpwl_quant_err);
        ok &= hpwl_ok;

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
    const bool real = argc >= 3 && fixture::load_from_args(3, argv, nl);
    if (!real) {
        fixture::SyntheticSpec spec;
        spec.max_large_degree = pinrec::MAX_LARGE_NET_DEGREE;   // every span group
        nl = fixture::build_synthetic(20260922u, spec);
        fixture::add_large_net_cases(nl);                     // split nodes, nets that must drop
    }
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u, golden::EXTENT),
                                            fixture::random_positions(nl.movable.size(), 12u, golden::EXTENT)};
    const golden::Lut lut = golden::make_lut();
    if (real) {
        const long capacity = argc >= 4 ? std::atol(argv[3]) : SLOT_CAPACITY;
        return run_config(nl.name.c_str(), nl, capacity, node_pos, lut, false) ? 0 : 1;
    }

    bool ok = true;
    packer::Chunked ch, tight;
    ok &= run_config("capacity=4096", nl, 4096, node_pos, lut, true, &ch);
    // Many small chunks: a consumer's tail holds blocks from many owners (C7). Bare cut at 2560
    // (30 chunks); the per-owner blocks' row rounding means 2048 and below fit no K (scan 2026-10-09). Meow.
    ok &= run_config("capacity=2560", nl, 2560, node_pos, lut, false, &tight, 0);
    ok &= run_config("capacity=1M", nl, SLOT_CAPACITY, node_pos, lut, false);

    // [5] the tail exchange's corner cases are present, so [1] exercised them. Meow.
    long streams = 0, max_blocks = 0, multi_row_blocks = 0;
    for (const packer::Chunk& c : tight.chunks) {
        streams += (long)c.tail_blocks.size();
        max_blocks = std::max(max_blocks, (long)c.tail_blocks.size());
        for (const auto& block : c.tail_blocks) multi_row_blocks += block.rows > 1;
    }
    const bool tails_covered = streams >= 50 && max_blocks >= 5 && multi_row_blocks >= 10;
    printf("%s [5] coverage: capacity=2560 has %ld tail blocks, up to %ld per tail, %ld of several rows\n",
           tails_covered ? "ok  " : "FAIL", streams, max_blocks, multi_row_blocks);
    ok &= tails_covered;

    // [5] coverage: the return path must carry real gradient. Share of |grad|^2 on nodes that are
    // external somewhere (their total needs the mailbox) -- a dropped return path would lose it.
    // Macros cross chunks as macros: a macro gradient folded from a borrowed macro's local pins (C6). Meow.
    long external_cells = 0, external_macros = 0;
    std::vector<char> held_external(ch.global.kind.size(), 0);
    for (const packer::Chunk& c : ch.chunks)
        for (size_t l = 0; l < c.work_node.size(); l++)
            if (c.external[l]) {
                held_external[c.work_node[l]] = 1;
                external_cells += ch.global.kind[c.work_node[l]] == packer::CELL;
                external_macros += ch.global.kind[c.work_node[l]] == packer::MACRO;
            }
    const std::vector<double> gx = golden::wa_gradient(nl, 0, node_pos[0], lut, true);
    double external_share = 0, total = 0;
    for (size_t node = 0; node < nl.movable.size(); node++) {
        if (!nl.movable[node]) continue;
        total += gx[node] * gx[node];
        if (held_external[node]) external_share += gx[node] * gx[node];
    }
    const bool covered = ch.num_chunks >= 3 && external_cells >= 100 && external_macros >= 2 && external_share > 0.05 * total;
    printf("%s [5] coverage: %d chunks, %ld external cells, %ld external macros, external nodes carry %.1f%% of |grad|^2\n",
           covered ? "ok  " : "FAIL", ch.num_chunks, external_cells, external_macros, 100.0 * external_share / total);
    ok &= covered;

    printf(ok ? "PASS: hpwl_gradient_CU\n" : "FAIL: hpwl_gradient_CU\n");
    return ok ? 0 : 1;
}
