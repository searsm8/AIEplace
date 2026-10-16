// Verify the #47 M2 golden (host/sw_flat/tail_fusion.hpp: the exchange as iteration_update will do it,
// option A) against B3's device exchange (src/modules/hpwl_gradient/tail_exchange.hpp, driven exactly as
// hpwl_gradient_CU's send_tails_pass and merge_tails_pass drive it). Both must agree BIT FOR BIT:
//
//   [1] SEND   every node-holding slot of every chunk image equal after all owners send; every
//              tail slot filled (no NaN left)
//   [2] SHORT  chunked_device_arrays(fill_tails = true), the bring-up shortcut, equals that image
//   [3] MERGE  every owned gradient equal after all owners merge, from random tail gradients of
//              mixed magnitude (so a different add order shows in the low bits)
//   [4] MUTANT the golden with its borrower order reversed must differ (needs slots with >= 2
//              borrowers, counted); with one tail entry dropped must differ. Proves [3] can fail.
//   [5] MUTANT the device with one chunk's shared prefix (first_private_slot, C8) a row short must
//              differ in both send and merge. Proves the passes' shortened reloads are checked.
//
// Synthetic design at capacity 4096 (FM) and 2560 (bare cut, many owners per tail); `--netlist FILE
// [CAPACITY]` runs [1]-[4] on a real design. Meow.

#include "tier1_stub.hpp"   // before the module
#include "record_design.hpp"
#include "modules/hpwl_gradient/tail_exchange.hpp"
#include "tail_fusion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

using namespace plalgo;

namespace {

ONCHIP_ARRAY float pos_URAM[pinrec::BANKS][ROWS_PER_BANK];
ONCHIP_ARRAY float grad_URAM[pinrec::BANKS][ROWS_PER_BANK];

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::vector<pinrec::SlotBeat> to_beats(const std::vector<float>& image) {
    std::vector<pinrec::SlotBeat> beats(image.size() / pinrec::LANES);
    std::memcpy(beats.data(), image.data(), image.size() * sizeof(float));
    return beats;
}
std::vector<float> to_floats(const std::vector<pinrec::SlotBeat>& beats) {
    std::vector<float> image(beats.size() * pinrec::LANES);
    std::memcpy(image.data(), beats.data(), image.size() * sizeof(float));
    return image;
}

// hpwl_gradient_CU's send_tails_pass. Meow.
std::vector<float> device_send(const packer::ChunkedDevice& dev) {
    std::vector<pinrec::SlotBeat> pos = to_beats(dev.slot_images);
    for (const pinrec::ChunkDesc& desc : dev.desc) {
        load_slot_array(pos.data() + desc.slot_beat_offset, desc.first_private_slot / pinrec::LANES, pos_URAM);
        for (int s = 0; s < desc.num_borrowers; s++)
            send_tail_block(dev.tail_blocks[desc.first_tail_block + s], dev.tail_ops.data(), pos_URAM, pos.data());
    }
    return to_floats(pos);
}

// hpwl_gradient_CU's merge_tails_pass. Meow.
std::vector<float> device_merge(const packer::ChunkedDevice& dev, const std::vector<float>& partial) {
    std::vector<pinrec::SlotBeat> grad = to_beats(partial);
    for (const pinrec::ChunkDesc& desc : dev.desc) {
        const int shared_slot_beats = desc.first_private_slot / pinrec::LANES;
        load_slot_array(grad.data() + desc.slot_beat_offset, shared_slot_beats, grad_URAM);
        for (int s = 0; s < desc.num_borrowers; s++)
            merge_tail_block(dev.tail_blocks[desc.first_tail_block + s], dev.tail_ops.data(), grad.data(), grad_URAM);
        drain_slot_array(grad_URAM, shared_slot_beats, grad.data() + desc.slot_beat_offset);
    }
    return to_floats(grad);
}

std::vector<float> golden_merge(const packer::Chunked& ch, std::vector<std::vector<sw_flat::TailParcel>> parcels,
                                const std::vector<float>& partial) {
    std::vector<float> grad = partial;
    for (int j = 0; j < ch.num_chunks; j++) sw_flat::fused_merge(ch, j, parcels[j], grad);
    return grad;
}

// Slots that hold a node, per chunk, in concatenated-image coordinates; tail = the packer's external. Meow.
struct SlotSets { std::vector<long> node_slots, external_slots, owned_slots; };
SlotSets slot_sets(const packer::Chunked& ch) {
    SlotSets sets;
    long base = 0;
    for (const packer::Chunk& c : ch.chunks) {
        for (size_t l = 0; l < c.work_node.size(); l++) {
            if (c.enc.node_slot[l] < 0) continue;
            const long slot = base + c.enc.node_slot[l];
            sets.node_slots.push_back(slot);
            if (c.external[l]) sets.external_slots.push_back(slot);
            else if (c.enc.node_slot[l] < c.enc.first_tail_slot) sets.owned_slots.push_back(slot);
        }
        base += c.enc.num_slots;
    }
    return sets;
}

long count_differences(const std::vector<float>& a, const std::vector<float>& b, const std::vector<long>& slots) {
    long differ = 0;
    for (long slot : slots) differ += !same_bits(a[slot], b[slot]);
    return differ;
}

bool run_config(const char* label, const packer::Netlist& nl, long capacity, int fm_passes, bool require_mutants) {
    packer::Config cfg;
    cfg.partition_fm_passes = fm_passes;
    const packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
    if (!ch.fits || packer::check_chunked(nl, ch, cfg)) { printf("FAIL [%s] chunked packer check\n", label); return false; }
    if (ch.num_chunks < 2) { printf("FAIL [%s] one chunk: nothing to exchange\n", label); return false; }
    const auto parcels = sw_flat::tail_parcels(ch);
    const SlotSets sets = slot_sets(ch);
    printf("     [%s] %d chunks, %ld external, %zu tail slots\n", label, ch.num_chunks, ch.externals, sets.external_slots.size());
    bool ok = true;

    for (int axis = 0; axis < 2; axis++) {
        const std::vector<float> node_pos = fixture::random_positions(nl.movable.size(), 21u + axis);
        const packer::ChunkedDevice dev = packer::chunked_device_arrays(ch, axis, node_pos);
        const std::vector<float> device = device_send(dev);
        std::vector<float> golden = dev.slot_images;
        for (int j = 0; j < ch.num_chunks; j++) sw_flat::fused_send(ch, j, parcels[j], golden);
        long unfilled = 0;
        for (long slot : sets.external_slots) unfilled += std::isnan(golden[slot]);
        const long send_differ = count_differences(device, golden, sets.node_slots);
        const bool send_ok = send_differ == 0 && unfilled == 0;
        printf("%s [1] [%s] axis %d send: %ld of %zu node slots differ, %ld tail slots unfilled\n", send_ok ? "ok  " : "FAIL",
               label, axis, send_differ, sets.node_slots.size(), unfilled);
        ok &= send_ok;

        const std::vector<float> shortcut = packer::chunked_device_arrays(ch, axis, node_pos, true).slot_images;
        const long short_differ = count_differences(shortcut, golden, sets.node_slots);
        printf("%s [2] [%s] axis %d fill_tails image: %ld node slots differ from the sent image\n", short_differ ? "FAIL" : "ok  ",
               label, axis, short_differ);
        ok &= short_differ == 0;
    }

    // Random partial gradients on every movable slot, tails included, as compute_pass drains them;
    // magnitudes spread over 6 decades so float addition order matters. Meow.
    const packer::ChunkedDevice dev = packer::chunked_device_arrays(ch, 0, std::vector<float>(nl.movable.size(), 0.0f));
    std::vector<float> partial(dev.slot_images.size(), NAN);
    std::mt19937 rng(31u);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f), decade(-3.0f, 3.0f);
    long base = 0;
    for (const packer::Chunk& c : ch.chunks) {
        for (long s = 0; s < c.enc.first_fixed_slot; s++) partial[base + s] = unit(rng) * std::pow(10.0f, decade(rng));
        base += c.enc.num_slots;
    }
    const std::vector<float> device = device_merge(dev, partial);
    const std::vector<float> golden = golden_merge(ch, parcels, partial);
    const long merge_differ = count_differences(device, golden, sets.owned_slots);
    printf("%s [3] [%s] merge: %ld of %zu owned gradients differ\n", merge_differ ? "FAIL" : "ok  ", label, merge_differ,
           sets.owned_slots.size());
    ok &= merge_differ == 0;

    // [4] mutants of the golden. Meow.
    std::vector<int> borrowers_of(dev.slot_images.size(), 0);
    for (const auto& owner : parcels) for (const auto& parcel : owner) for (long slot : parcel.owner_slot) borrowers_of[slot]++;
    long multi = 0;
    for (long slot : sets.owned_slots) multi += borrowers_of[slot] >= 2;
    auto reversed = parcels;
    for (auto& owner : reversed) std::reverse(owner.begin(), owner.end());
    const long reversed_differ = count_differences(device, golden_merge(ch, reversed, partial), sets.owned_slots);
    auto dropped = parcels;
    for (auto& owner : dropped) if (!owner.empty()) { owner.back().owner_slot.pop_back(); owner.back().tail_slot.pop_back(); break; }
    const long dropped_differ = count_differences(device, golden_merge(ch, dropped, partial), sets.owned_slots);
    const bool mutants_ok = dropped_differ > 0 && (multi == 0 ? !require_mutants : reversed_differ > 0);
    printf("%s [4] [%s] mutants: reversed borrowers %ld differ (%ld slots with >= 2 borrowers), one entry dropped %ld differ\n",
           mutants_ok ? "ok  " : "FAIL", label, reversed_differ, multi, dropped_differ);
    ok &= mutants_ok;

    // [5] the largest shared prefix, one row short: its last row holds a shared node in some bank. Meow.
    packer::ChunkedDevice short_dev = dev;
    auto widest = std::max_element(short_dev.desc.begin(), short_dev.desc.end(),
        [](const pinrec::ChunkDesc& a, const pinrec::ChunkDesc& b) { return a.first_private_slot < b.first_private_slot; });
    widest->first_private_slot -= pinrec::BANKS;
    std::vector<float> sent = dev.slot_images;
    for (int j = 0; j < ch.num_chunks; j++) sw_flat::fused_send(ch, j, parcels[j], sent);
    const long short_send_differ = count_differences(device_send(short_dev), sent, sets.node_slots);
    const long short_merge_differ = count_differences(device_merge(short_dev, partial), golden, sets.owned_slots);
    const bool prefix_ok = short_send_differ > 0 && short_merge_differ > 0;
    printf("%s [5] [%s] shared prefix a row short: send %ld differ, merge %ld differ\n", prefix_ok ? "ok  " : "FAIL", label,
           short_send_differ, short_merge_differ);
    ok &= prefix_ok;
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    packer::Netlist nl;
    if (argc >= 3 && fixture::load_from_args(3, argv, nl)) {
        const long capacity = argc >= 4 ? std::atol(argv[3]) : SLOT_CAPACITY;
        const bool ok = run_config(nl.name.c_str(), nl, capacity, 1, false);
        printf(ok ? "PASS: tail_exchange\n" : "FAIL: tail_exchange\n");
        return ok ? 0 : 1;
    }
    fixture::SyntheticSpec spec;
    spec.max_large_degree = pinrec::MAX_LARGE_NET_DEGREE;
    nl = fixture::build_synthetic(20260922u, spec);
    fixture::add_large_net_cases(nl);
    bool ok = true;
    ok &= run_config("capacity=4096", nl, 4096, 1, false);
    ok &= run_config("capacity=2560", nl, 2560, 0, true);   // many owners per tail: the order mutant must bite
    printf(ok ? "PASS: tail_exchange\n" : "FAIL: tail_exchange\n");
    return ok ? 0 : 1;
}
