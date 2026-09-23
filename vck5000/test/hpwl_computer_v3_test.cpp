// Verify hpwl_computer_v3 (bring_up/hpwl_computer_v3) -- per-net HPWL for a design split into
// chunks, ghost positions exchanged through DDR (#41).
//
// Golden: identical to hpwl_computer_v2_test -- each in-scope net's span, in float, straight from
// the PARSED netlist -- so it is independent of the partition, the homes, the ghosts, and the
// exchange layout. Bit-exact. Macro-pin slots AND ghost slots arrive as NaN and the exchange buffer
// starts as NaN, so a missed refresh, export or import cannot pass.
//
//   [1] chunked:   capacity 2048 slots (the ~5.5K-slot fixture splits into several chunks)
//   [2] unchunked: capacity 1M (one chunk: the export pass is skipped; must equal v2's behaviour)
//   [3] coverage:  several chunks, ghosts of cells AND of macro pins, fixed pins copied to several chunks
//
// `hpwl_computer_v3_test --bookshelf DIR NAME [CAPACITY]` / `--def FILE NAME [CAPACITY]` runs [1] on
// a real design (random positions); CAPACITY defaults to the on-chip SLOT_CAPACITY. Meow.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "record_design.hpp"
#include "modules/hpwl_computer_v3.hpp"

#include <cstdio>

using namespace plalgo;

static std::vector<OutBeat> run_axis(const packer::Chunked& ch, int axis, const std::vector<float>& node_pos) {
    const packer::ChunkedDevice dev = packer::chunked_device_arrays(ch, axis, node_pos);
    std::vector<pinrec::RecordBeat> records(dev.records.size() / pinrec::LANES);
    std::memcpy(records.data(), dev.records.data(), dev.records.size() * sizeof(uint32_t));
    std::vector<pinrec::SlotBeat> pos(dev.slot_images.size() / pinrec::LANES);
    std::memcpy(pos.data(), dev.slot_images.data(), dev.slot_images.size() * sizeof(float));
    std::vector<float> exchange(dev.exchange_size + 1, NAN);   // +1: never zero-length
    std::vector<OutBeat> out(records.size());
    hpwl_computer_v3(dev.desc.data(), (int)dev.desc.size(), records.data(), pos.data(), dev.macro_pins.data(),
                     dev.import_slots.data(), dev.export_slots.data(), dev.blocks.data(), exchange.data(),
                     dev.offset_table.data(), (int)dev.offset_table.size(), out.data(), dev.offset_bits);
    return out;
}

static float golden_span(const packer::Netlist& nl, int net, int axis, const std::vector<float>& node_pos) {
    float hi = -INFINITY, lo = INFINITY;
    for (const packer::Pin& pin : nl.nets[net]) {
        const float p = fixture::pin_position(nl, pin, axis, node_pos);
        hi = p > hi ? p : hi; lo = p < lo ? p : lo;
    }
    return hi - lo;
}

static bool run_config(const char* label, const packer::Netlist& nl, long capacity, const std::vector<float> node_pos[2],
                       packer::Chunked* keep = nullptr) {
    const packer::Config cfg;
    packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
    if (packer::check_chunked(nl, ch, cfg)) { printf("FAIL [%s] chunked packer check\n", label); return false; }
    long max_slots = 0;
    for (const auto& c : ch.chunks) max_slots = std::max(max_slots, c.enc.num_slots);
    if (max_slots > SLOT_CAPACITY) { printf("FAIL [%s] a chunk exceeds on-chip capacity\n", label); return false; }
    long bad = 0, nets_seen = 0;
    for (int axis = 0; axis < 2; axis++) {
        const std::vector<OutBeat> out = run_axis(ch, axis, node_pos[axis]);
        long base = 0;
        for (const packer::Chunk& c : ch.chunks) {
            for (size_t position = 0; position < c.enc.issue.size(); position++)
                for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
                    const int local = c.enc.net_at[position * pinrec::MAX_NETS_PER_BEAT + k];
                    const float want = local == packer::NO_NET ? 0.0f : golden_span(nl, c.net_global[local], axis, node_pos[axis]);
                    nets_seen += axis == 0 && local != packer::NO_NET;
                    const float got = out[base + position].v[k];
                    if (!(got == want)) {
                        if (bad < 5) printf("  FAIL axis %d chunk-position %ld net slot %d: got %.9g want %.9g\n",
                                            axis, base + (long)position, k, got, want);
                        bad++;
                    }
                }
            base += (long)c.enc.issue.size();
        }
    }
    long in_scope = 0;
    for (const auto& net : nl.nets) in_scope += packer::in_scope(net);
    const bool ok = bad == 0 && nets_seen == in_scope;
    printf("%s [%s] %d chunks, %ld ghosts, exchange %ld floats, %ld/%ld nets x 2 axes, %ld mismatches\n",
           ok ? "ok  " : "FAIL", label, ch.num_chunks, ch.ghosts, ch.exchange_size, nets_seen, in_scope, bad);
    if (keep) *keep = std::move(ch);
    return ok;
}

int main(int argc, char** argv) {
    packer::Netlist nl;
    const bool real = argc >= 4 && fixture::load_from_args(4, argv, nl);
    if (!real) nl = fixture::build_synthetic(20260922u);
    const std::vector<float> node_pos[2] = {fixture::random_positions(nl.movable.size(), 11u),
                                            fixture::random_positions(nl.movable.size(), 12u)};
    if (real) {
        const long capacity = argc >= 5 ? std::atol(argv[4]) : SLOT_CAPACITY;
        return run_config(nl.name.c_str(), nl, capacity, node_pos) ? 0 : 1;
    }

    bool ok = true;
    packer::Chunked ch;
    ok &= run_config("capacity=2048", nl, 2048, node_pos, &ch);
    ok &= run_config("capacity=1M", nl, SLOT_CAPACITY, node_pos);

    // [3] coverage
    long ghost_macro_pins = 0, ghost_cells = 0;
    std::vector<int> fixed_copies(ch.global.kind.size(), 0);
    for (const packer::Chunk& c : ch.chunks)
        for (size_t l = 0; l < c.work_node.size(); l++) {
            const packer::NodeKind kind = ch.global.kind[c.work_node[l]];
            if (c.ghost[l]) { ghost_macro_pins += kind == packer::MACRO_PIN; ghost_cells += kind == packer::CELL; }
            if (kind == packer::FIXED_PIN) fixed_copies[c.work_node[l]]++;
        }
    const long multi_copy_fixed = std::count_if(fixed_copies.begin(), fixed_copies.end(), [](int n) { return n > 1; });
    const bool covered = ch.num_chunks >= 3 && ghost_cells >= 100 && ghost_macro_pins >= 10 && multi_copy_fixed >= 10;
    printf("%s [3] coverage: %d chunks, %ld cell ghosts, %ld macro-pin ghosts, %ld fixed pins copied to >1 chunk\n",
           covered ? "ok  " : "FAIL", ch.num_chunks, ghost_cells, ghost_macro_pins, multi_copy_fixed);
    ok &= covered;

    printf(ok ? "PASS: hpwl_computer_v3\n" : "FAIL: hpwl_computer_v3\n");
    return ok ? 0 : 1;
}
