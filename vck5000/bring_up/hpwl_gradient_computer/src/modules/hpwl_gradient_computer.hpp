#ifndef PL_ALGO_HPWL_GRADIENT_COMPUTER_HPP
#define PL_ALGO_HPWL_GRADIENT_COMPUTER_HPP

// hpwl_gradient_computer -- WA-wirelength gradient per node from the static pin-record stream (#41),
// one axis per call. Extends hpwl_computer_v2 (same on-chip gather, same Dhar bbox trees) with the
// rest of Dhar's Method 1, and does the pin->node summation as an on-chip scatter-add:
//
//   load_pos / refresh_macros / load_offsets   as hpwl_computer_v2
//   zero_grad        grad_URAM[movable slots] = 0, 16 slots per cycle
//   beat_loop        gather -> bbox trees -> per-pin exp terms (Fig. 5) -> four sum trees
//                    (dhar_tree<AddOp>, Fig. 6/7) -> per-net 1/B^2 -> per-pin combiner (Fig. 8,
//                    eq. 4) -> merge lanes of a repeated node -> grad[node_slot] += g
//   fold_macros      grad[macro] = sum of its macro-pin slots' gradients
//   drain_grad       grad_URAM[movable slots] -> DDR, 16 slots per beat
//
// Same math as sw_only computeHpwlPartials_CPU (Partials.cpp) and hpwl_gradient_dhar: exponents
// shifted by the net's bbox, exp from the host LUT exp(-d/gamma) with linear interpolation.
//
// The scatter-add is bank-major like the gather: each bank finds the one run head addressing it
// and does one read-add-write. The host keeps a node's updates HAZARD_DISTANCE beats apart; that
// distance is declared to HLS as a TRUE dependence, so HLS must fit the RMW round trip inside it
// (or raise II) -- the schedule and the pragma are one contract. Meow.

#include "modules/hpwl_computer_v2.hpp"

namespace plalgo {

constexpr int GRAD_LUT_MAX = 1024;   // production LUT is 242 entries (GAMMA_MULT / PLACE_STEP_NORM + 2)
constexpr int EXP_LOOKUPS  = 2 * pinrec::LANES;   // a+ and a- per lane

// (lut[i], lut[i+1]) in one word: an interpolated lookup is ONE read (D8). Meow.
struct LutPair { float lo; float hi; };

struct AddOp { static inline float apply(float a, float b) { return a + b; } };

// Same arithmetic as hpwl_lut_exp, so the result is bit-identical to the pl_algo module's. Meow.
static inline float lut_exp_pair(const LutPair lut_BRAM[GRAD_LUT_MAX], int lut_size, float inv_lut_step, float d) {
#pragma HLS INLINE
    const float idx_f = d * inv_lut_step;
    const int   idx   = (int)idx_f;
    if (idx >= lut_size - 1) return 0.0f;
    const float frac  = idx_f - (float)idx;
    const LutPair pair = lut_BRAM[idx];
    return pair.lo * (1.0f - frac) + pair.hi * frac;
}

// One copy per concurrent lookup: 32 lookups per beat, one read each. Meow.
static void load_exp_lut(const float* exp_lut_DDR, int lut_size, LutPair lut_BRAM[EXP_LOOKUPS][GRAD_LUT_MAX]) {
    float prev = 0.0f;
load_lut:
    for (int i = 0; i < lut_size; i++) {
#pragma HLS PIPELINE II=1
        const float value = exp_lut_DDR[i];
        if (i > 0)
            for (int copy = 0; copy < EXP_LOOKUPS; copy++) lut_BRAM[copy][i - 1] = LutPair{prev, value};
        prev = value;
    }
    for (int copy = 0; copy < EXP_LOOKUPS; copy++) lut_BRAM[copy][lut_size - 1] = LutPair{prev, 0.0f};   // never read: idx < lut_size-1
}

static void fill_slot_array(int num_slot_beats, float value, float dst_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
fill_slots:
    for (int b = 0; b < num_slot_beats; b++) {
#pragma HLS PIPELINE II=1
        for (int j = 0; j < pinrec::LANES; j++) {
            if (b & 1) dst_URAM[j + pinrec::LANES][b >> 1] = value;
            else       dst_URAM[j][b >> 1]                 = value;
        }
    }
}

static void drain_slot_array(const float src_URAM[pinrec::BANKS][ROWS_PER_BANK], int num_slot_beats,
                             pinrec::SlotBeat* dst_DDR) {
drain_slots:
    for (int b = 0; b < num_slot_beats; b++) {
#pragma HLS PIPELINE II=1
        pinrec::SlotBeat beat;
        for (int j = 0; j < pinrec::LANES; j++)
            beat.v[j] = (b & 1) ? src_URAM[j + pinrec::LANES][b >> 1] : src_URAM[j][b >> 1];
        dst_DDR[b] = beat;
    }
}

// Macro-pin gradients fold into their macro. The list is grouped by macro_slot, so a run of
// entries accumulates in a register and the macro is written once at the run's end. A macro has
// no pin of its own on any net (every one was rewritten to a macro-pin slot), so its slot holds 0
// until this write. Reads macro-pin slots, writes macro slots: disjoint. Meow.
static void fold_macro_pins(const pinrec::MacroPinRef* macro_pins_DDR, int num_macro_pins,
                            float grad_URAM[pinrec::BANKS][ROWS_PER_BANK]) {
    int   macro_slot = -1;
    float macro_grad = 0.0f;
fold_macros:
    for (int e = 0; e < num_macro_pins; e++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM inter false
        const pinrec::MacroPinRef ref = macro_pins_DDR[e];
        if (ref.macro_slot != macro_slot) {
            if (macro_slot >= 0) grad_URAM[macro_slot % pinrec::BANKS][macro_slot / pinrec::BANKS] = macro_grad;
            macro_slot = ref.macro_slot;
            macro_grad = 0.0f;
        }
        macro_grad += grad_URAM[ref.pin_slot % pinrec::BANKS][ref.pin_slot / pinrec::BANKS];
    }
    if (macro_slot >= 0) grad_URAM[macro_slot % pinrec::BANKS][macro_slot / pinrec::BANKS] = macro_grad;
}

// The per-net sums the combiner needs, one set per net position of the beat. Meow.
struct NetSums {
    float Bp[pinrec::MAX_NETS_PER_BEAT], Cp[pinrec::MAX_NETS_PER_BEAT];
    float Bm[pinrec::MAX_NETS_PER_BEAT], Cm[pinrec::MAX_NETS_PER_BEAT];
};

// The record stream -> scatter-added gradient (and per-net HPWL), against positions already
// resident in pos_URAM and a grad_URAM the caller has zeroed. Meow.
static void gradient_beat_loop(const pinrec::RecordBeat* records_DDR, int num_beats,
                               const int beat_count_REG[pinrec::NET_DEGREES_PROCESSED],
                               const float pos_URAM[pinrec::BANKS][ROWS_PER_BANK],
                               const float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX],
                               const LutPair lut_BRAM[EXP_LOOKUPS][GRAD_LUT_MAX], int lut_size,
                               float inv_lut_step, float inv_gamma,
                               float grad_URAM[pinrec::BANKS][ROWS_PER_BANK], int first_fixed_slot,
                               OutBeat* out_beats_DDR, int offset_bits) {
beat_loop:
    for (int beat = 0; beat < num_beats; beat++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=grad_URAM type=inter direction=RAW distance=pinrec::HAZARD_DISTANCE dependent=true
        const int degree = resolve_degree(beat, beat_count_REG);
        const int degree_idx = degree - pinrec::MIN_NET_DEGREE;
        const int nets_per_beat = pinrec::LANES / degree;
        const DecodedLanes d = decode_lanes(records_DDR[beat], offset_bits);

        float x[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=x complete dim=0
        gather_pin_positions(d, pos_URAM, offset_BRAM, x);

        // ---- bbox trees (v1): per-net max / min, then each lane's own net's pair ----
        TreeOutputs t = build_tree_outputs(x);
#pragma HLS ARRAY_PARTITION variable=t.max_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=t.min_deg complete dim=0
        int segment[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=segment complete dim=0
        for (int i = 0; i < pinrec::LANES; i++) segment[i] = i / degree;

        // ---- term generators (Fig. 5): exponents shifted by the bbox, so every a is in [0, 1] ----
        float a_plus[pinrec::LANES], a_minus[pinrec::LANES];
        float Bp_terms[pinrec::LANES], Cp_terms[pinrec::LANES], Bm_terms[pinrec::LANES], Cm_terms[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=a_plus complete dim=0
#pragma HLS ARRAY_PARTITION variable=a_minus complete dim=0
#pragma HLS ARRAY_PARTITION variable=Bp_terms complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cp_terms complete dim=0
#pragma HLS ARRAY_PARTITION variable=Bm_terms complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cm_terms complete dim=0
        for (int i = 0; i < pinrec::LANES; i++) {
            const float net_max = t.max_deg[degree_idx][segment[i]];
            const float net_min = t.min_deg[degree_idx][segment[i]];
            a_plus[i]  = d.empty[i] ? 0.0f : lut_exp_pair(lut_BRAM[2 * i],     lut_size, inv_lut_step, net_max - x[i]);
            a_minus[i] = d.empty[i] ? 0.0f : lut_exp_pair(lut_BRAM[2 * i + 1], lut_size, inv_lut_step, x[i] - net_min);
            Bp_terms[i] = a_plus[i];  Cp_terms[i] = a_plus[i] * x[i];
            Bm_terms[i] = a_minus[i]; Cm_terms[i] = a_minus[i] * x[i];
        }

        // ---- four sum trees (Fig. 6/7): every contiguous-segment sum for every degree ----
        float Bp_deg[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
        float Cp_deg[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
        float Bm_deg[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
        float Cm_deg[pinrec::NET_DEGREES_PROCESSED][pinrec::MAX_NETS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=Bp_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cp_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=Bm_deg complete dim=0
#pragma HLS ARRAY_PARTITION variable=Cm_deg complete dim=0
        dhar_tree<AddOp>(Bp_terms, Bp_deg);
        dhar_tree<AddOp>(Cp_terms, Cp_deg);
        dhar_tree<AddOp>(Bm_terms, Bm_deg);
        dhar_tree<AddOp>(Cm_terms, Cm_deg);

        // ---- per net position: select this degree's sums, one 1/B^2 per net (not per pin) ----
        NetSums s;
        float inv_Bp2[pinrec::MAX_NETS_PER_BEAT], inv_Bm2[pinrec::MAX_NETS_PER_BEAT];
#pragma HLS ARRAY_PARTITION variable=s.Bp complete dim=0
#pragma HLS ARRAY_PARTITION variable=s.Cp complete dim=0
#pragma HLS ARRAY_PARTITION variable=s.Bm complete dim=0
#pragma HLS ARRAY_PARTITION variable=s.Cm complete dim=0
#pragma HLS ARRAY_PARTITION variable=inv_Bp2 complete dim=0
#pragma HLS ARRAY_PARTITION variable=inv_Bm2 complete dim=0
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            s.Bp[k] = Bp_deg[degree_idx][k]; s.Cp[k] = Cp_deg[degree_idx][k];
            s.Bm[k] = Bm_deg[degree_idx][k]; s.Cm[k] = Cm_deg[degree_idx][k];
            inv_Bp2[k] = 1.0f / (s.Bp[k] * s.Bp[k]);
            inv_Bm2[k] = 1.0f / (s.Bm[k] * s.Bm[k]);
        }

        // ---- combiners (Fig. 8, eq. 4): each pin's WA partial ----
        float g[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=g complete dim=0
        for (int i = 0; i < pinrec::LANES; i++) {
            const int k = segment[i];
            g[i] = ((1.0f + x[i] * inv_gamma) * s.Bp[k] - s.Cp[k] * inv_gamma) * (a_plus[i] * inv_Bp2[k])
                 - ((1.0f - x[i] * inv_gamma) * s.Bm[k] + s.Cm[k] * inv_gamma) * (a_minus[i] * inv_Bm2[k]);
        }

        // ---- merge: a node's repeated pins sit in adjacent lanes with the same slot; a segmented
        //      suffix scan (Hillis-Steele, 4 steps) leaves each run's total in its head lane ----
        bool same_as_next[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=same_as_next complete dim=0
        for (int i = 0; i < pinrec::LANES; i++)
            same_as_next[i] = i + 1 < pinrec::LANES && !d.empty[i] && d.slot[i] == d.slot[i + 1];
        for (int step = 1; step < pinrec::LANES; step *= 2) {
            float next_g[pinrec::LANES];
#pragma HLS ARRAY_PARTITION variable=next_g complete dim=0
            for (int i = 0; i < pinrec::LANES; i++) {
                const bool joined = i + step < pinrec::LANES && !d.empty[i] && d.slot[i] == d.slot[i + step];
                next_g[i] = joined ? g[i] + g[i + step] : g[i];
            }
            for (int i = 0; i < pinrec::LANES; i++) g[i] = next_g[i];
        }

        // ---- scatter-add, bank-major: bank b takes the one movable run head addressing it ----
        for (int b = 0; b < pinrec::BANKS; b++) {
            bool     hit = false;
            uint32_t row = 0;
            float    add = 0.0f;
            for (int i = 0; i < pinrec::LANES; i++) {
                const bool head = i == 0 || !same_as_next[i - 1];
                if (head && !d.empty[i] && d.slot[i] < (uint32_t)first_fixed_slot && d.slot[i] % pinrec::BANKS == (uint32_t)b) {
                    hit = true; row = d.slot[i] / pinrec::BANKS; add = g[i];
                }
            }
            if (hit) grad_URAM[b][row] += add;
        }

        // ---- HPWL by-product, as hpwl_computer_v2 ----
        OutBeat out_beat;
#pragma HLS ARRAY_PARTITION variable=out_beat.v complete dim=0
        for (int k = 0; k < pinrec::MAX_NETS_PER_BEAT; k++) {
            const bool live = k < nets_per_beat && !d.empty[k * degree];
            out_beat.v[k] = live ? t.max_deg[degree_idx][k] - t.min_deg[degree_idx][k] : 0.0f;
        }
        out_beats_DDR[beat] = out_beat;
    }

}

static void hpwl_gradient_computer(
        const pinrec::RecordBeat*  records_DDR,       // [num_beats] static stream, this axis
        int                        num_beats,
        const int*                 beat_count_DDR,    // [NET_DEGREES_PROCESSED]
        const pinrec::SlotBeat*    pos_DDR,           // [num_slot_beats] slot-major positions, this axis
        int                        num_slot_beats,
        const pinrec::MacroPinRef* macro_pins_DDR,    // [num_macro_pins] refresh + fold list, this axis
        int                        num_macro_pins,
        const float*               offset_table_DDR,  // [offset_table_size] this axis
        int                        offset_table_size,
        const float*               exp_lut_DDR,       // [lut_size] exp(-t) table
        int                        lut_size,
        OutBeat*                   out_beats_DDR,     // [num_beats] per-net HPWL (by-product)
        pinrec::SlotBeat*          grad_DDR,          // [first_fixed_slot / LANES] gradient, movable slots
        int                        first_fixed_slot,
        int                        offset_bits,
        float                      inv_gamma,
        float                      inv_lut_step) {

    ONCHIP_ARRAY float pos_URAM[pinrec::BANKS][ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=pos_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=pos_URAM type=ram_2p impl=uram
    ONCHIP_ARRAY float grad_URAM[pinrec::BANKS][ROWS_PER_BANK];
#pragma HLS ARRAY_PARTITION variable=grad_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=grad_URAM type=ram_2p impl=uram
    ONCHIP_ARRAY float offset_BRAM[pinrec::LANES][OFFSET_TABLE_MAX];
#pragma HLS ARRAY_PARTITION variable=offset_BRAM complete dim=1
    ONCHIP_ARRAY LutPair lut_BRAM[EXP_LOOKUPS][GRAD_LUT_MAX];
#pragma HLS ARRAY_PARTITION variable=lut_BRAM complete dim=1

    int beat_count_REG[pinrec::NET_DEGREES_PROCESSED];
#pragma HLS ARRAY_PARTITION variable=beat_count_REG complete dim=0
cache_counts:
    for (int k = 0; k < pinrec::NET_DEGREES_PROCESSED; k++) beat_count_REG[k] = beat_count_DDR[k];

    const int movable_slot_beats = first_fixed_slot / pinrec::LANES;
    load_slot_array(pos_DDR, num_slot_beats, pos_URAM);
    refresh_macro_pins(macro_pins_DDR, num_macro_pins, pos_URAM);
    load_offset_table(offset_table_DDR, offset_table_size, offset_BRAM);
    load_exp_lut(exp_lut_DDR, lut_size, lut_BRAM);
    fill_slot_array(movable_slot_beats, 0.0f, grad_URAM);

    gradient_beat_loop(records_DDR, num_beats, beat_count_REG, pos_URAM, offset_BRAM, lut_BRAM, lut_size,
                       inv_lut_step, inv_gamma, grad_URAM, first_fixed_slot, out_beats_DDR, offset_bits);

    fold_macro_pins(macro_pins_DDR, num_macro_pins, grad_URAM);
    drain_slot_array(grad_URAM, movable_slot_beats, grad_DDR);
}

} // namespace plalgo

#endif // PL_ALGO_HPWL_GRADIENT_COMPUTER_HPP
