#ifndef SW_FLAT_DEVICE_LOOP_HPP
#define SW_FLAT_DEVICE_LOOP_HPP

// device_loop.hpp -- SKELETON, nothing here runs yet (Mark, 2026-10-27): the device run's top-level
// loop, in the same shape and with the same call names as sw_flat's Placer::run (placer/placer.cpp),
// so every call has a CPU golden of the same name:
//
//     initialization();
//     while (running) { compute_hpwl_partials(); compute_electric_fields(); iteration_update(); }
//
// It fixes, per call, which compute unit runs it, which device-resident arrays it reads and writes,
// and which scalars cross to the host. Positions, gradients and density maps stay in DDR between
// calls (#45 D6); the gamma / lambda schedule and the stop test stay on the host. Becomes #45 step 7's
// `--backend device`; the kernel top is pl/src/top.cpp (not written). Status, 2026-10-27:
//
//   compute_hpwl_partials    hpwl_gradient_CU: built, bit-exact on the card at 230 MHz. Until #47 B4
//                            it still runs its own borrowed send / merge passes (B3).
//   compute_electric_fields  bin_density_CU: in progress (#48). The field solve (DCT pre/post on the
//                            PL, FFT on the AIE): vck5000/ legacy only (#45 step 6).
//   iteration_update         not built (#45 step 6): the borrowed merge, force gather + combine, the
//                            BB norms, then (after the host's schedule) the step and the borrowed send.
// Meow.

#include "format/density_footprint.hpp"
#include "format/pin_record.hpp"

#include <cstdint>

namespace sw_flat::device {

// Slot-major images, 16 slots a beat: every chunk's rows (owned, borrowed, macro pins, fixed pins),
// then the fillers. [2] = one per axis; the device runs the axes in turn (one axis's URAM is 256 of
// 463). Meow.
struct DeviceArrays {
    // Written once by the host, from the packer.
    const pinrec::ChunkDesc*      chunks;
    const int32_t*                group_ends;
    const pinrec::RecordBeat*     records[2];
    const pinrec::MacroBeat*      macro_beats[2];
    const pinrec::SlotBeat*       macro_offsets[2];
    const float*                  offset_table[2];
    const densrec::FootprintBeat* footprint_idx;
    const densrec::SlowSlot*      slow_slots;
    const densrec::Footprint*     footprints;
    const densrec::FixedBeat*     fixed_base;     // the fixed nodes' capped deposit (#35)
    const pinrec::SlotBeat*       num_pins;       // per slot, constant: the preconditioner's pin term (fillers 0)
    const pinrec::SlotBeat*       area;           // per slot, constant: its area term, times lambda

    // Per iteration: Placer's SlotState pair (current = iteration k, next = k + 1).
    pinrec::SlotBeat* node_pos[2][2];     // [current, next] u
    pinrec::SlotBeat* probe_pos[2][2];    // [current, next] v: the gradients read next, borrowed rows included
    pinrec::SlotBeat* probe_grad[2][2];   // [current, next] grad f(v): wirelength, then combined
    pinrec::SlotBeat* rho;                // bin density, bins * bins
    pinrec::SlotBeat* field[2];           // E per bin, per axis
};

// Host -> device, set by the host's schedule before the call that uses it. Meow.
struct Schedule {
    float inv_gamma, inv_lut_step;      // compute_hpwl_partials
    float density_weight;               // lambda: iteration_update's combine and preconditioner
    float precond_coef;
    float step_length, momentum_coeff;  // iteration_update's step
};

// Device -> host: what the schedule, the BB step and the stop test need. Meow.
struct Report {
    int64_t  hpwl_fixed[2];             // compute_hpwl_partials, per axis (hpwl_to_fixed units)
    uint64_t overflow_excess;           // compute_electric_fields (overflow_from_excess on the host)
    float    pos_norm_sq, grad_norm_sq; // iteration_update: |v_next - v|^2, |P (g_next - g)|^2
    float    gwl_L1, gden_L1;           // iteration_update: lambda's and the preconditioner's norms
};

// Upload the arrays; iteration zero's two gradients (lambda_0 and the BB estimate's trial step, as
// Placer::initialization); then step 1. Meow.
void initialization(DeviceArrays& dev, Schedule& schedule, Report& report);

// hpwl_gradient_CU, once per axis: reads records, macro_beats, macro_offsets, offset_table and
// probe_pos[next] (borrowed rows already sent); writes probe_grad[next] (owned and borrowed rows) and
// report.hpwl_fixed. Meow.
void compute_hpwl_partials(DeviceArrays& dev, const Schedule& schedule, Report& report);

// bin_density_CU, then the field solve: reads probe_pos[next] (both axes), footprint_idx, slow_slots,
// footprints and fixed_base; writes rho, field and report.overflow_excess. Meow.
void compute_electric_fields(DeviceArrays& dev, const Schedule& schedule, Report& report);

// Placer::iteration_update across the device and the host. Device, streaming each owner's slots
// (#47 B4): add the borrowers' borrowed gradients into probe_grad[next], gather the force from field,
// combine, and reduce report's norms. Host: the BB step length, metrics, schedule and stop test.
// Device, unless stopping: the preconditioned Nesterov step into [next] after [current] = [next], the
// die clamp, and each owner's new probe position sent into its borrowers' borrowed rows. Returns false
// once the host's stop test fires. The step can later ride at the head of the next call's stream, so
// an iteration is one launch (#45 step 7). Meow.
bool iteration_update(DeviceArrays& dev, Schedule& schedule, Report& report);

inline void run(DeviceArrays& dev)
{
    Schedule schedule{};
    Report report{};
    initialization(dev, schedule, report);
    bool running = true;
    while (running) {
        compute_hpwl_partials(dev, schedule, report);
        compute_electric_fields(dev, schedule, report);
        running = iteration_update(dev, schedule, report);
    }
}

} // namespace sw_flat::device

#endif // SW_FLAT_DEVICE_LOOP_HPP
