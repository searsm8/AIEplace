// record_stream.cpp -- hpwl_gradient_CU's arithmetic on the CPU (#45 step 5b), so sw_flat can be the
// bit-exact golden for the composed device run. Three [sw_flat] switches, each measured on its own:
//   wa_sums = "tree"       per-net B/C sums over the record lanes, in the Dhar trees' order (wa_sums.hpp);
//                          a large net sums its beats through the window tree. The combiner itself is
//                          already wa_gradient.hpp's expression term for term (wirelength.cpp).
//   grad_merge = "stream"  each beat's repeated-slot lanes merged by the Hillis-Steele scan, scattered
//                          in stream order; macro pins folded in list order; tails merged into their
//                          owners, borrowers ascending (hpwl_gradient_CU.hpp's three passes).
//   hpwl = "fixed"         the schedule's HPWL as an int64 sum of hpwl_to_fixed per net and axis, the
//                          stream's nets only (hpwl_CU.hpp). The float conversion of the total is ours:
//                          the device returns the int64s.
// The device runs the axes one call each; the arithmetic per axis is independent, so both go here
// at once. The Dhar and window trees are included from pl/src, not retyped: they ARE the summation
// order, and pl/test holds their sums against a double golden. test/record_stream_check.cpp holds
// this file against hpwl_gradient_CU bit for bit. Meow.

#include "placer.hpp"

#include "modules/hpwl_gradient/dhar_tree.hpp"
#include "modules/hpwl_gradient/record_gather.hpp"   // balanced_tree

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <unordered_map>

namespace sw_flat {

using pinrec::LANES;

namespace {

struct AddOp {   // wa_sums.hpp's, which sits behind an HLS stream include
    static inline float apply(float a, float b) { return a + b; }
    static inline float identity() { return 0.0f; }
};

[[noreturn]] void fail(const std::string& why)
{
    fprintf(stderr, "sw_flat: record stream: %s\n", why.c_str());
    exit(1);
}

// One axis of one beat's lanes: positions and exp terms, the four tree inputs. Meow.
struct BeatTerms {
    float x[LANES], a_plus[LANES], a_minus[LANES];
    float Bp[LANES], Cp[LANES], Bm[LANES], Cm[LANES];
};

} // namespace

void Placer::build_record_stream()
{
    // The packer's net n is FlatDesign's n-th net of 2..96 pins, in file order (to_netlist). Meow.
    std::vector<int> design_net;
    for (int net = 0; net < design.num_nets(); net++)
        if (design.net_degree(net) >= pinrec::MIN_NET_DEGREE && design.net_degree(net) <= packer::IGNORE_NET_DEGREE)
            design_net.push_back(net);
    std::vector<char> carried(design_net.size(), 0);
    RecordStream& rs = stream;
    rs.chunk_first_position.assign(1, 0);
    rs.chunk_first_fold.assign(1, 0);

    for (int k = 0; k < chunked.num_chunks; k++) {
        const packer::Chunk& c = chunked.chunks[k];
        const packer::Encoded& e = c.enc;
        const long base_slot = layout.chunk_first_slot[k];
        const long first_position = rs.chunk_first_position.back();
        const long end_position = first_position + (long)e.issue.size();
        rs.degree.resize(end_position, 0);
        rs.lane_pin.resize(end_position * LANES, -1);
        rs.lane_slot.resize(end_position * LANES, -1);
        rs.lane_movable.resize(end_position * LANES, 0);

        // A net's pins in lane order: encode_records' stable sort by node slot (packer rule B3). Meow.
        auto lane_order = [&](int local_net, std::vector<int> pins) {
            std::stable_sort(pins.begin(), pins.end(), [&](int a, int b) {
                return e.node_slot[e.work.nets[local_net][a].node] < e.node_slot[e.work.nets[local_net][b].node];
            });
            return pins;
        };
        auto place = [&](long position, int lane, int local_net, int pin_index) {
            const int net = design_net[c.net_global[local_net]];
            const int pin = design.net_first_pin[net] + pin_index;
            const int local_node = e.work.nets[local_net][pin_index].node;
            const long slot = base_slot + e.node_slot[local_node];
            if (layout.node[slot] != design.pin_node[pin]) fail("a lane's slot is not its pin's node, chunk " + std::to_string(k));
            const long index = position * LANES + lane;
            rs.lane_pin[index] = pin;
            rs.lane_slot[index] = slot;
            rs.lane_movable[index] = e.node_slot[local_node] < e.first_fixed_slot;
            carried[c.net_global[local_net]] = 1;
        };

        std::map<int, std::vector<long>> large_beats;   // local net -> its real beats
        for (long p = 0; p < (long)e.issue.size(); p++) {
            if (e.issue[p] == packer::BUBBLE) continue;
            const packer::Beat& beat = e.beats[e.issue[p]];
            const long position = first_position + p;
            if (beat.span > 1) {
                rs.degree[position] = LANES;
                const std::vector<int> pins = lane_order(beat.nets[0], beat.pins);
                for (int lane = 0; lane < (int)pins.size(); lane++) place(position, lane, beat.nets[0], pins[lane]);
                large_beats[beat.nets[0]].push_back(position);
                continue;
            }
            rs.degree[position] = beat.degree;
            rs.small_positions.push_back(position);
            for (size_t n = 0; n < beat.nets.size(); n++) {
                std::vector<int> pins(beat.degree);
                std::iota(pins.begin(), pins.end(), 0);
                pins = lane_order(beat.nets[n], pins);
                for (int i = 0; i < beat.degree; i++) place(position, (int)n * beat.degree + i, beat.nets[n], pins[i]);
            }
        }
        for (const auto& [local_net, positions] : large_beats) {
            if ((int)positions.size() != e.beats[e.issue[positions[0] - first_position]].span) fail("a large net's beats do not match its span");
            rs.large_first.push_back((long)rs.large_positions.size());
            rs.large_positions.insert(rs.large_positions.end(), positions.begin(), positions.end());
        }
        rs.chunk_first_position.push_back(end_position);

        for (const pinrec::MacroPinRef& ref : packer::macro_pin_refs(e, 0)) {
            if (ref.pin_slot == pinrec::MACRO_PIN_SKIP) continue;
            rs.folds.push_back({base_slot + ref.pin_slot, base_slot + ref.macro_slot,
                                (ref.flags & pinrec::MACRO_PIN_FIRST) != 0, (ref.flags & pinrec::MACRO_PIN_LAST) != 0});
        }
        rs.chunk_first_fold.push_back((long)rs.folds.size());
        rs.chunk_movable_end.push_back(base_slot + e.first_fixed_slot);
    }
    rs.large_first.push_back((long)rs.large_positions.size());

    // merge_tails_pass: owners ascending; per owner its borrowers ascending (chunked_device_arrays). Meow.
    const std::vector<long> owner_slot = packer::owner_slots_of(chunked);
    for (int owner = 0; owner < chunked.num_chunks; owner++)
        for (int borrower = 0; borrower < chunked.num_chunks; borrower++) {
            const packer::Chunk& c = chunked.chunks[borrower];
            for (const packer::Chunk::TailBlock& block : c.tail_blocks) {
                if (block.owner != owner) continue;
                for (long i = block.first; i < block.first + block.count; i++) {
                    const int local = c.tail_order[i];
                    rs.tail_merges.emplace_back(layout.chunk_first_slot[owner] + owner_slot[c.work_node[local]],
                                                layout.chunk_first_slot[borrower] + c.enc.node_slot[local]);
                }
            }
        }

    for (size_t n = 0; n < design_net.size(); n++) {
        if (carried[n]) rs.nets.push_back(design_net[n]);
        else rs.dropped_nets++;
    }
    lane_grad.assign(rs.lane_pin.size(), {});
    long merged_lanes = 0;   // lanes the scan folds into the lane before: a node's repeated pins (B3)
    for (size_t lane = 1; lane < rs.lane_pin.size(); lane++)
        merged_lanes += lane % LANES != 0 && rs.lane_pin[lane] >= 0 && rs.lane_slot[lane] == rs.lane_slot[lane - 1];
    printf("sw_flat: record stream: %d chunks, %ld positions, %zu nets, %ld dropped by the packer, %ld merged lanes, %zu folds, %zu tail merges\n",
           chunked.num_chunks, rs.chunk_first_position.back(), rs.nets.size(), rs.dropped_nets, merged_lanes,
           rs.folds.size(), rs.tail_merges.size());
}

void Placer::compute_stream_partials()
{
    const bool use_lut = cfg.wa_exp_lut;
    const float inv_lut_step = 1.0f / (PL_LUT_STEP_NORM * gamma);   // the pl_algo host's
    const RecordStream& rs = stream;

    // wa_sums.hpp's term generators; exact exp in compute_net_partials' expression. An EMPTY lane
    // weighs nothing. Meow.
    auto fill_terms = [&](BeatTerms& t, const float hi[LANES], const float lo[LANES], const bool empty[LANES]) {
        for (int i = 0; i < LANES; i++) {
            if (empty[i])     { t.a_plus[i] = 0.0f;                                   t.a_minus[i] = 0.0f; }
            else if (use_lut) { t.a_plus[i] = lut_exp(exp_lut, inv_lut_step, hi[i] - t.x[i]);
                                t.a_minus[i] = lut_exp(exp_lut, inv_lut_step, t.x[i] - lo[i]); }
            else              { t.a_plus[i] = std::exp((t.x[i] - hi[i]) * inv_gamma);
                                t.a_minus[i] = std::exp((lo[i] - t.x[i]) * inv_gamma); }
            t.Bp[i] = t.a_plus[i];  t.Cp[i] = t.a_plus[i] * t.x[i];
            t.Bm[i] = t.a_minus[i]; t.Cm[i] = t.a_minus[i] * t.x[i];
        }
    };
    // wa_gradient.hpp's combiner. Meow.
    auto combine = [&](const BeatTerms& t, int i, float Bp, float Cp, float Bm, float Cm) {
        if (Bp == 0 || Bm == 0) nan_detected = true;
        const float inv_Bp2 = 1.0f / (Bp * Bp), inv_Bm2 = 1.0f / (Bm * Bm);
        const float g = ((1.0f + t.x[i] * inv_gamma) * Bp - Cp * inv_gamma) * (t.a_plus[i] * inv_Bp2)
                      - ((1.0f - t.x[i] * inv_gamma) * Bm + Cm * inv_gamma) * (t.a_minus[i] * inv_Bm2);
        if (g != g) nan_detected = true;
        return g;
    };
    auto set_grad = [&](long lane, int axis, float g) { (axis ? lane_grad[lane].y : lane_grad[lane].x) = g; };
    auto lane_x = [&](long lane, int axis) {
        const Position p = pin_position(*this, rs.lane_pin[lane], true);
        return axis ? p.y : p.x;
    };

    // Small-net beats: net n of degree d holds lanes [n*d, (n+1)*d) (rule B2), its sums one Dhar output. Meow.
    #pragma omp parallel for schedule(dynamic, 256)
    for (size_t b = 0; b < rs.small_positions.size(); b++) {
        const long position = rs.small_positions[b];
        const int degree = rs.degree[position], nets_per_beat = LANES / degree;
        const long lane0 = position * LANES;
        bool empty[LANES];
        for (int i = 0; i < LANES; i++) empty[i] = rs.lane_pin[lane0 + i] < 0;
        for (int axis = 0; axis < 2; axis++) {
            BeatTerms t;
            float hi[LANES], lo[LANES];
            for (int i = 0; i < LANES; i++) { t.x[i] = empty[i] ? 0.0f : lane_x(lane0 + i, axis); hi[i] = lo[i] = t.x[i]; }
            for (int n = 0; n < nets_per_beat; n++) {
                if (empty[n * degree]) continue;
                float net_hi = t.x[n * degree], net_lo = net_hi;
                for (int i = n * degree; i < (n + 1) * degree; i++) { net_hi = std::max(net_hi, t.x[i]); net_lo = std::min(net_lo, t.x[i]); }
                for (int i = n * degree; i < (n + 1) * degree; i++) { hi[i] = net_hi; lo[i] = net_lo; }
            }
            fill_terms(t, hi, lo, empty);
            float Bp[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT], Cp[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
            float Bm[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT], Cm[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
            plalgo::dhar_tree<AddOp>(t.Bp, Bp); plalgo::dhar_tree<AddOp>(t.Cp, Cp);
            plalgo::dhar_tree<AddOp>(t.Bm, Bm); plalgo::dhar_tree<AddOp>(t.Cm, Cm);
            const int d = degree - pinrec::MIN_NET_DEGREE;
            for (int i = 0; i < nets_per_beat * degree; i++) {
                if (empty[i]) continue;
                const int n = i / degree;
                set_grad(lane0 + i, axis, combine(t, i, Bp[d][n], Cp[d][n], Bm[d][n], Cm[d][n]));
            }
        }
    }

    // Large nets: each real beat's whole-beat sums (the degree-16 Dhar output), then the window tree
    // over the net's beats, newest first. EMPTY lanes take lane 0's position (hpwl_CU.hpp). Meow.
    const long num_large = (long)rs.large_first.size() - 1;
    #pragma omp parallel for schedule(dynamic, 16)
    for (long net = 0; net < num_large; net++) {
        const long first = rs.large_first[net], span = rs.large_first[net + 1] - first;
        for (int axis = 0; axis < 2; axis++) {
            std::vector<BeatTerms> beats(span);
            std::vector<std::array<bool, LANES>> empty(span);
            float net_hi = -std::numeric_limits<float>::infinity(), net_lo = std::numeric_limits<float>::infinity();
            for (long s = 0; s < span; s++) {
                const long lane0 = rs.large_positions[first + s] * LANES;
                for (int i = 0; i < LANES; i++) {
                    empty[s][i] = rs.lane_pin[lane0 + i] < 0;
                    beats[s].x[i] = empty[s][i] ? beats[s].x[0] : lane_x(lane0 + i, axis);
                    if (!empty[s][i]) { net_hi = std::max(net_hi, beats[s].x[i]); net_lo = std::min(net_lo, beats[s].x[i]); }
                }
            }
            float window[4][pinrec::MAX_SPAN];   // Bp, Cp, Bm, Cm; newest first
            for (auto& sums : window) std::fill(sums, sums + pinrec::MAX_SPAN, 0.0f);
            float hi[LANES], lo[LANES];
            std::fill(hi, hi + LANES, net_hi);
            std::fill(lo, lo + LANES, net_lo);
            for (long s = 0; s < span; s++) {
                fill_terms(beats[s], hi, lo, empty[s].data());
                float out[4][pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
                plalgo::dhar_tree<AddOp>(beats[s].Bp, out[0]); plalgo::dhar_tree<AddOp>(beats[s].Cp, out[1]);
                plalgo::dhar_tree<AddOp>(beats[s].Bm, out[2]); plalgo::dhar_tree<AddOp>(beats[s].Cm, out[3]);
                for (int q = 0; q < 4; q++) window[q][span - 1 - s] = out[q][LANES - pinrec::MIN_NET_DEGREE][0];
            }
            float sums[4];
            for (int q = 0; q < 4; q++) sums[q] = plalgo::balanced_tree<AddOp>(window[q], (int)span);
            for (long s = 0; s < span; s++) {
                const long lane0 = rs.large_positions[first + s] * LANES;
                for (int i = 0; i < LANES; i++)
                    if (!empty[s][i]) set_grad(lane0 + i, axis, combine(beats[s], i, sums[0], sums[1], sums[2], sums[3]));
            }
        }
    }
}

void Placer::merge_stream_gradient()
{
    const RecordStream& rs = stream;
    if (!cfg.wa_sums_tree) {
        #pragma omp parallel for schedule(static)
        for (size_t lane = 0; lane < rs.lane_pin.size(); lane++)
            if (rs.lane_pin[lane] >= 0) lane_grad[lane] = pin_partials[rs.lane_pin[lane]];
    }

    // compute_pass, per chunk (disjoint slots, so the chunks run in parallel). Meow.
    #pragma omp parallel for schedule(dynamic, 1)
    for (int k = 0; k < chunked.num_chunks; k++) {
        for (long slot = layout.chunk_first_slot[k]; slot < rs.chunk_movable_end[k]; slot++) next.probe_grad[slot].clear();
        for (long position = rs.chunk_first_position[k]; position < rs.chunk_first_position[k + 1]; position++) {
            if (rs.degree[position] == 0) continue;
            const long lane0 = position * LANES;
            Gradient g[LANES];
            for (int i = 0; i < LANES; i++) g[i] = lane_grad[lane0 + i];
            // wa_gradient.hpp's segmented suffix scan: each run of one slot leaves its total in its head. Meow.
            for (int step = 1; step < LANES; step *= 2) {
                Gradient next_g[LANES];
                for (int i = 0; i < LANES; i++) {
                    const bool joined = i + step < LANES && rs.lane_pin[lane0 + i] >= 0 && rs.lane_slot[lane0 + i] == rs.lane_slot[lane0 + i + step];
                    next_g[i] = g[i];
                    if (joined) next_g[i] += g[i + step];
                }
                std::copy(next_g, next_g + LANES, g);
            }
            for (int i = 0; i < LANES; i++) {
                const bool head = i == 0 || rs.lane_slot[lane0 + i - 1] != rs.lane_slot[lane0 + i];
                if (head && rs.lane_pin[lane0 + i] >= 0 && rs.lane_movable[lane0 + i]) next.probe_grad[rs.lane_slot[lane0 + i]] += g[i];
            }
        }
        // fold_macro_pins: FIRST starts a macro's sum, LAST writes it over the macro's gradient. Meow.
        std::unordered_map<long, Gradient> running;
        for (long f = rs.chunk_first_fold[k]; f < rs.chunk_first_fold[k + 1]; f++) {
            const RecordStream::Fold& fold = rs.folds[f];
            Gradient sum = next.probe_grad[fold.pin_slot];
            if (!fold.first) { sum = running[fold.macro_slot]; sum += next.probe_grad[fold.pin_slot]; }
            if (fold.last) next.probe_grad[fold.macro_slot] = sum;
            else running[fold.macro_slot] = sum;
        }
    }
    for (const auto& [owner_slot, tail_slot] : rs.tail_merges) next.probe_grad[owner_slot] += next.probe_grad[tail_slot];
}

float Placer::stream_wirelength(bool at_probe) const
{
    int64_t totals[2];
    stream_hpwl_fixed(at_probe, totals);
    return (float)std::ldexp((double)(totals[0] + totals[1]), -pinrec::HPWL_FRAC_BITS);
}

void Placer::stream_hpwl_fixed(bool at_probe, int64_t totals[2]) const
{
    int64_t total_x = 0, total_y = 0;
    #pragma omp parallel for schedule(static) reduction(+ : total_x, total_y)
    for (size_t n = 0; n < stream.nets.size(); n++) {
        const int net = stream.nets[n], first = design.net_first_pin[net];
        const Position first_pos = pin_position(*this, first, at_probe);
        float min_x = first_pos.x, max_x = first_pos.x, min_y = first_pos.y, max_y = first_pos.y;
        for (int i = 1; i < design.net_degree(net); i++) {
            const Position p = pin_position(*this, first + i, at_probe);
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
        }
        total_x += pinrec::hpwl_to_fixed(max_x - min_x);
        total_y += pinrec::hpwl_to_fixed(max_y - min_y);
    }
    totals[0] = total_x;
    totals[1] = total_y;
}

} // namespace sw_flat
