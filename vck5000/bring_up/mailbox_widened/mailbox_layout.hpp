#ifndef MAILBOX_LAYOUT_HPP
#define MAILBOX_LAYOUT_HPP

// mailbox_layout.hpp -- host side of the WIDENED mailbox (#42): the same external values as
// Chunked's one-float-per-entry mailbox, but 16 entries per 512-bit beat, so send / receive /
// return / collect each move a beat per cycle instead of a float.
//
// A mailbox beat is one parcel's (owner j -> consumer k) entries, one per lane. The device reads
// both URAM sides bank-major (as the beat loop's gather and scatter-add do), so per beat:
//   W1  the owner slots sit in distinct banks           (send gathers, collect scatter-adds them)
//   W2  the consumer slots sit in distinct banks        (receive scatters, return gathers them)
//   W3  an owner slot is not repeated within HAZARD_DISTANCE beats of owner j's sequence (its
//       parcels, k ascending): collect is a read-add-write. Within a parcel owner slots are
//       distinct, so only a parcel boundary can break it; blocked entries wait, and an all-empty
//       beat is emitted when nothing else fits.
// Each parcel is a whole number of beats. Layout is consumer-major like Chunked's: inbox k holds
// parcels j ascending, so the consumer still reads ONE sequential run.
//
// Per lane the two sides carry the SAME entry, so collect adds in the same per-slot order as the
// 1-float loops (parcel k ascending): the results are bit-identical, not merely close. Meow.

#include "beat_packer.hpp"

#include <unordered_set>

namespace packer {

struct WideMailbox {
    long mailbox_beats = 0;
    long entries = 0;
    long hazard_beats = 0;                              // all-empty beats emitted for W3 alone
    std::vector<std::vector<int32_t>>   shared_lanes;   // owner j: LANES per beat, own slot or -1, owner sequence
    std::vector<std::vector<pinrec::ParcelRef>> shared_parcels;  // owner j: {mailbox beat, beat count}, k ascending
    std::vector<std::vector<int32_t>>   external_lanes; // consumer k: LANES per beat, local slot or -1, inbox order
    std::vector<long> inbox_beat_offset;
    std::vector<int>  lane_node;                        // per mailbox lane: global work node, -1 empty (checker only)
};

namespace wide_detail {
struct Entry { int node; int32_t owner_slot, consumer_slot; };

// Greedy: each beat takes the fullest (owner bank, consumer bank) buckets first, so the bank
// loads drain evenly and the beat count stays at its lower bound max(ceil(n/16), busiest bank). Meow.
inline std::vector<std::vector<Entry>> group_parcel(const std::vector<Entry>& entries, std::deque<std::vector<int32_t>>& recent_beats,
                                                    int hazard, long& hazard_beats) {
    std::vector<std::vector<std::vector<Entry>>> buckets(BANKS, std::vector<std::vector<Entry>>(BANKS));
    for (const Entry& e : entries) buckets[e.owner_slot % BANKS][e.consumer_slot % BANKS].push_back(e);
    long remaining = (long)entries.size();
    std::vector<std::vector<Entry>> beats;
    while (remaining > 0) {
        std::unordered_set<int32_t> blocked;
        for (const auto& beat : recent_beats) blocked.insert(beat.begin(), beat.end());
        std::vector<std::tuple<long, int, int>> order;
        for (int ob = 0; ob < BANKS; ob++)
            for (int cb = 0; cb < BANKS; cb++)
                if (!buckets[ob][cb].empty()) order.emplace_back(-(long)buckets[ob][cb].size(), ob, cb);
        std::sort(order.begin(), order.end());
        std::vector<char> owner_used(BANKS, 0), consumer_used(BANKS, 0);
        std::vector<Entry> beat;
        for (const auto& [neg_size, ob, cb] : order) {
            if ((int)beat.size() == LANES) break;
            if (owner_used[ob] || consumer_used[cb]) continue;
            auto& bucket = buckets[ob][cb];
            for (size_t i = bucket.size(); i-- > 0;) {
                if (blocked.count(bucket[i].owner_slot)) continue;
                beat.push_back(bucket[i]);
                bucket.erase(bucket.begin() + i);
                owner_used[ob] = consumer_used[cb] = 1;
                remaining--;
                break;
            }
        }
        if (beat.empty()) hazard_beats++;
        std::vector<int32_t> owner_slots;
        for (const Entry& e : beat) owner_slots.push_back(e.owner_slot);
        recent_beats.push_back(owner_slots);
        if ((int)recent_beats.size() >= hazard) recent_beats.pop_front();
        beats.push_back(std::move(beat));
    }
    return beats;
}
} // namespace wide_detail

inline WideMailbox build_wide_mailbox(const Chunked& ch, const Config& cfg) {
    using wide_detail::Entry;
    const int K = ch.num_chunks;
    const long num_nodes = (long)ch.global.kind.size();
    std::vector<std::vector<int>> local_in(K, std::vector<int>(num_nodes, -1));
    for (int k = 0; k < K; k++)
        for (size_t l = 0; l < ch.chunks[k].work_node.size(); l++) local_in[k][ch.chunks[k].work_node[l]] = (int)l;

    std::vector<std::vector<std::vector<Entry>>> parcel_entries(K, std::vector<std::vector<Entry>>(K));   // [j][k]
    WideMailbox wm;
    for (int k = 0; k < K; k++) {
        const Chunk& c = ch.chunks[k];
        for (int local : c.external_local) {
            const int node = c.work_node[local];
            const int j = ch.owner[node];
            parcel_entries[j][k].push_back({node, (int32_t)ch.chunks[j].enc.node_slot[local_in[j][node]], (int32_t)c.enc.node_slot[local]});
            wm.entries++;
        }
    }
    std::vector<std::vector<std::vector<std::vector<Entry>>>> parcel_beats(K, std::vector<std::vector<std::vector<Entry>>>(K));
    for (int j = 0; j < K; j++) {
        std::deque<std::vector<int32_t>> recent_beats;   // owner j's last hazard-1 beats, across its parcels
        for (int k = 0; k < K; k++)
            if (!parcel_entries[j][k].empty())
                parcel_beats[j][k] = wide_detail::group_parcel(parcel_entries[j][k], recent_beats, cfg.hazard, wm.hazard_beats);
    }

    wm.shared_lanes.assign(K, {});
    wm.shared_parcels.assign(K, {});
    wm.external_lanes.assign(K, {});
    wm.inbox_beat_offset.assign(K, 0);
    std::vector<std::vector<long>> parcel_offset(K, std::vector<long>(K, 0));
    for (int k = 0; k < K; k++) {
        wm.inbox_beat_offset[k] = wm.mailbox_beats;
        for (int j = 0; j < K; j++) {
            parcel_offset[j][k] = wm.mailbox_beats;
            for (const auto& beat : parcel_beats[j][k]) {
                for (int lane = 0; lane < LANES; lane++) {
                    const bool used = lane < (int)beat.size();
                    wm.external_lanes[k].push_back(used ? beat[lane].consumer_slot : -1);
                    wm.lane_node.push_back(used ? beat[lane].node : -1);
                }
                wm.mailbox_beats++;
            }
        }
    }
    for (int j = 0; j < K; j++)
        for (int k = 0; k < K; k++) {
            if (parcel_beats[j][k].empty()) continue;
            wm.shared_parcels[j].push_back({(int32_t)parcel_offset[j][k], (int32_t)parcel_beats[j][k].size()});
            for (const auto& beat : parcel_beats[j][k])
                for (int lane = 0; lane < LANES; lane++) wm.shared_lanes[j].push_back(lane < (int)beat.size() ? beat[lane].owner_slot : -1);
        }
    return wm;
}

// Verifies W1-W3, that every external (node, consumer) pair travels exactly once, and that both
// sides of each lane name the same node. Returns the number of violations. Meow.
inline long check_wide_mailbox(const Chunked& ch, const WideMailbox& wm, const Config& cfg) {
    const int K = ch.num_chunks;
    long failures = 0;
    auto bank_distinct = [&](const int32_t* lanes) {
        uint64_t seen = 0;
        for (int lane = 0; lane < LANES; lane++) {
            if (lanes[lane] < 0) continue;
            const uint64_t bit = 1ull << (lanes[lane] % BANKS);
            if (seen & bit) return false;
            seen |= bit;
        }
        return true;
    };
    long expected = 0, travelled = 0;
    for (int k = 0; k < K; k++) {
        const Chunk& c = ch.chunks[k];
        expected += (long)c.external_local.size();
        std::vector<char> received(c.work_node.size(), 0);
        std::vector<long> slot_local(c.enc.num_slots, -1);
        for (size_t l = 0; l < c.work_node.size(); l++) if (c.enc.node_slot[l] >= 0) slot_local[c.enc.node_slot[l]] = (long)l;
        const long beats = (long)wm.external_lanes[k].size() / LANES;
        for (long b = 0; b < beats; b++) {
            const int32_t* lanes = &wm.external_lanes[k][b * LANES];
            failures += !bank_distinct(lanes);   // W2
            for (int lane = 0; lane < LANES; lane++) {
                const int node = wm.lane_node[(wm.inbox_beat_offset[k] + b) * LANES + lane];
                if (lanes[lane] < 0) { failures += node >= 0; continue; }
                const long local = slot_local[lanes[lane]];
                if (local < 0 || !c.external[local] || c.work_node[local] != node || received[local]++) failures++;
                travelled++;
            }
        }
    }
    failures += expected != travelled || travelled != wm.entries;
    for (int j = 0; j < K; j++) {
        const Chunk& c = ch.chunks[j];
        std::vector<long> slot_node(c.enc.num_slots, -1);
        for (size_t l = 0; l < c.work_node.size(); l++) if (c.enc.node_slot[l] >= 0) slot_node[c.enc.node_slot[l]] = c.work_node[l];
        std::vector<long> last_beat(c.enc.num_slots, -(1L << 40));
        long sequence = 0, lane_entry = 0;
        for (const pinrec::ParcelRef& parcel : wm.shared_parcels[j])
            for (int b = 0; b < parcel.count; b++, sequence++, lane_entry += LANES) {
                const int32_t* lanes = &wm.shared_lanes[j][lane_entry];
                failures += !bank_distinct(lanes);   // W1
                for (int lane = 0; lane < LANES; lane++) {
                    const int node = wm.lane_node[(long)(parcel.offset + b) * LANES + lane];
                    if (lanes[lane] < 0) { failures += node >= 0; continue; }
                    if (slot_node[lanes[lane]] != node || ch.owner[node] != j) failures++;
                    if (sequence - last_beat[lanes[lane]] < cfg.hazard) failures++;   // W3
                    last_beat[lanes[lane]] = sequence;
                }
            }
    }
    return failures;
}

} // namespace packer

#endif // MAILBOX_LAYOUT_HPP
