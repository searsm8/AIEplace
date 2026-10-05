// Verify mailbox_widened (bring_up/mailbox_widened, #42) -- the four mailbox loops at 16 entries
// per beat -- against hpwl_gradient_computer_v2's one-float loops on the same Chunked design.
//
// Golden: the 1-float send / receive / return / collect, run on identical URAM images. The widened
// path moves the same values and adds each slot's contributions in the same order (parcel k
// ascending), so every comparison is BIT-IDENTICAL (memcmp, NaN external slots included).
//
//   [1] LAYOUT     check_wide_mailbox: W1/W2 bank-distinct lanes, W3 hazard, every external travels once
//   [2] POSITIONS  after send + receive, every chunk's pos URAM equals the 1-float path's
//   [3] GRADIENTS  after return + collect, every chunk's grad URAM equals the 1-float path's
//   [4] COVERAGE   capacity 2048 gives K >= 3 (owners with several parcels, so W3 crosses a parcel
//                  boundary) and lane use >= 60% (observed 2026-10-02: 73%; small parcels round up to
//                  whole beats -- the real chunked designs reach 99.6%+)
//
// `mailbox_widened_test --bookshelf DIR NAME [CAPACITY]` runs [1]-[3] on a real design. Meow.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "record_design.hpp"
#include "modules/hpwl_gradient_computer_v2.hpp"
#include "modules/mailbox_widened.hpp"
#include "mailbox_layout.hpp"

#include <cstdio>
#include <cstring>

using namespace plalgo;

constexpr double MIN_LANE_USE = 0.60;

struct Uram {   // one float[BANKS][ROWS_PER_BANK] array, heap-allocated. Meow.
    std::vector<float> data = std::vector<float>((size_t)pinrec::BANKS * ROWS_PER_BANK, 0.0f);
    float (*banks())[ROWS_PER_BANK] { return reinterpret_cast<float (*)[ROWS_PER_BANK]>(data.data()); }
    void set(long slot, float value) { data[(size_t)(slot % pinrec::BANKS) * ROWS_PER_BANK + slot / pinrec::BANKS] = value; }
};

static bool same(const std::vector<Uram>& a, const std::vector<Uram>& b) {
    for (size_t k = 0; k < a.size(); k++)
        if (std::memcmp(a[k].data.data(), b[k].data.data(), a[k].data.size() * sizeof(float))) return false;
    return true;
}

// Runs [1]-[3]; returns failures and reports lane use. Meow.
static int verify(const packer::Chunked& ch, const char* label, double& lane_use) {
    const packer::Config cfg;
    const packer::WideMailbox wm = packer::build_wide_mailbox(ch, cfg);
    const long layout_failures = packer::check_wide_mailbox(ch, wm, cfg);
    lane_use = (double)wm.entries / std::max<long>(1, wm.mailbox_beats * pinrec::LANES);
    int failures = 0;

    const int K = ch.num_chunks;
    const std::vector<float> node_pos = fixture::random_positions(ch.global.kind.size(), 7u);
    const packer::ChunkedDevice dev = packer::chunked_device_arrays(ch, 0, node_pos);
    std::vector<Uram> pos_ref(K), pos_wide(K), grad_ref(K), grad_wide(K);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    for (int k = 0; k < K; k++) {
        const std::vector<float> image = packer::chunk_slot_positions(ch, k, 0, node_pos);
        for (size_t slot = 0; slot < image.size(); slot++) {
            pos_ref[k].set((long)slot, image[slot]);
            grad_ref[k].set((long)slot, unit(rng));
        }
        pos_wide[k] = pos_ref[k];
        grad_wide[k] = grad_ref[k];
    }

    std::vector<float> mailbox(std::max<long>(1, ch.mailbox_size), NAN);
    std::vector<pinrec::SlotBeat> mailbox_wide(std::max<long>(1, wm.mailbox_beats));
    for (auto& beat : mailbox_wide) for (float& v : beat.v) v = NAN;
    auto lanes = [](const std::vector<int32_t>& v) { return reinterpret_cast<const LaneBeat*>(v.data()); };
    auto inbox_beats = [&](int k) { return (int)(wm.external_lanes[k].size() / pinrec::LANES); };

    for (int j = 0; j < K; j++) {
        send_external_positions(dev.desc[j], dev.parcels.data(), dev.shared_slots.data(), pos_ref[j].banks(), mailbox.data());
        send_wide(wm.shared_parcels[j].data(), (int)wm.shared_parcels[j].size(), lanes(wm.shared_lanes[j]), pos_wide[j].banks(), mailbox_wide.data());
    }
    for (int k = 0; k < K; k++) {
        receive_external_positions(dev.desc[k], dev.external_slots.data(), mailbox.data(), pos_ref[k].banks());
        receive_wide(lanes(wm.external_lanes[k]), mailbox_wide.data() + wm.inbox_beat_offset[k], inbox_beats(k), pos_wide[k].banks());
    }
    const bool positions_ok = same(pos_ref, pos_wide);

    for (int k = 0; k < K; k++) {
        return_external_gradients(dev.desc[k], dev.external_slots.data(), grad_ref[k].banks(), mailbox.data());
        return_wide(lanes(wm.external_lanes[k]), grad_wide[k].banks(), mailbox_wide.data() + wm.inbox_beat_offset[k], inbox_beats(k));
    }
    for (int j = 0; j < K; j++) {
        collect_external_gradients(dev.desc[j], dev.parcels.data(), dev.shared_slots.data(), mailbox.data(), grad_ref[j].banks());
        collect_wide(wm.shared_parcels[j].data(), (int)wm.shared_parcels[j].size(), lanes(wm.shared_lanes[j]), mailbox_wide.data(), grad_wide[j].banks());
    }
    const bool gradients_ok = same(grad_ref, grad_wide);

    printf("%s %s: K=%d, %ld entries -> %ld beats (lane use %.2f%%, %ld hazard beats) | [1] layout %ld violations | [2] positions %s | [3] gradients %s\n",
           (layout_failures || !positions_ok || !gradients_ok) ? "FAIL" : "ok  ", label, K, wm.entries, wm.mailbox_beats,
           100.0 * lane_use, wm.hazard_beats, layout_failures, positions_ok ? "identical" : "DIFFER", gradients_ok ? "identical" : "DIFFER");
    failures += layout_failures > 0;
    failures += !positions_ok;
    failures += !gradients_ok;
    return failures;
}

int main(int argc, char** argv) {
    const packer::Config cfg;
    if (argc >= 4) {
        const std::string kind = argv[1];
        const packer::Netlist nl = kind == "--def" ? packer::read_def(argv[2], argv[3]) : packer::read_bookshelf(argv[2], argv[3]);
        const long capacity = argc > 4 ? std::atol(argv[4]) : SLOT_CAPACITY;
        const packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
        double lane_use = 0;
        return verify(ch, nl.name.c_str(), lane_use) ? 1 : 0;
    }

    int failures = 0;
    const packer::Netlist nl = fixture::build_synthetic(20261002u);
    for (long capacity : {2048L, 4096L}) {
        const packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
        char label[64];
        snprintf(label, sizeof label, "synthetic cap=%ld", capacity);
        double lane_use = 0;
        failures += verify(ch, label, lane_use);
        if (capacity == 2048) {   // [4]
            const bool coverage = ch.num_chunks >= 3 && ch.externals > 0 && lane_use >= MIN_LANE_USE;
            printf("%s [4] coverage: K=%d (need >= 3), lane use %.2f%% (need >= %.0f%%)\n", coverage ? "ok  " : "FAIL",
                   ch.num_chunks, 100.0 * lane_use, 100.0 * MIN_LANE_USE);
            failures += !coverage;
        }
    }
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
