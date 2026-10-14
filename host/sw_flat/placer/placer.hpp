#ifndef SW_FLAT_PLACER_HPP
#define SW_FLAT_PLACER_HPP

// placer.hpp -- sw_flat's CPU phase-1 global placer (#45 step 5): sw_only's algorithm on flat arrays
// in device slot order (slot_layout.hpp), at PL v1 scope (D6: phase 1 only, no backtracking).
//
// BIT-IDENTICAL to sw_only under the same config (Mark, 2026-10-09). The state lives in slot order,
// but every order-sensitive float sum runs in sw_only's order through the stored orders below:
//   movable_slots   components that are not FIXED, in NAME order (sw_only's std::map), then the
//                   fillers. The deposit, the initial draws, and every OrderedReduce sum use it.
//   fixed_nodes     FIXED components (not IO pads), in name order: the fixed deposit.
//   nets            gradient replay in FILE order (DataBase::mv_nets); the HPWL metric in NAME order.
// That is the default. [sw_flat] slot_order and wa_exp switch sums to slot order and exp to the PL's
// table (#45 step 5b), each measured on its own against the bit-identical default.
//
// Files follow sw_only's (host/src/sw_only/src/placer/): setup.cpp = Setup.cpp, wirelength.cpp =
// Partials.cpp + the HPWL metric, density.cpp = Density.cpp + Grid.cpp, step.cpp = Step.cpp,
// schedule.cpp = Schedule.cpp + BestSolution.cpp, main.cpp = AIEplace.cpp + the trace. A function
// named like sw_only's computes what sw_only's does, with the same float expression. Meow.

#include "fillers.hpp"
#include "flat_design.hpp"
#include "slot_layout.hpp"

#include "Common.h"   // AIEplace::Position / Gradient: the same float operators as sw_only's Node

#include <chrono>
#include <limits>
#include <string>
#include <vector>

namespace sw_flat {

using AIEplace::Gradient;
using AIEplace::Position;

// The PL exp table's geometry: the host's PLACE_STEP_NORM (host/src/pl_algo/src/Placement.hpp) and
// pl/test's GAMMA_MULT; exp reads 0 past 12 gamma. Meow.
constexpr float PL_LUT_STEP_NORM  = 0.05f;
constexpr int   PL_LUT_GAMMA_MULT = 12;

// The run_config.toml [params] keys sw_only reads on the phase-1 path, with its defaults. Meow.
struct PlacerConfig {
    std::string benchmark, output_dir;
    float init_gamma = 0;
    bool  gamma_schedule = false, gamma_bin_scaled = true;
    float gamma_ref_grid = 512.0f;
    float init_step_seed = 0.01f;
    float target_density = 0.9f;                 // maximum_utilization; placement.constraints overrides it
    bool  enable_momentum = true, enable_preconditioning = true, precond_coef_escalation = true;
    bool  enable_density_clamp = true, dct_normalize = true, enable_filler = true, enable_phase2 = true;
    int   bins_per_row = 0;                      // 0 = the ePlace formula
    int   ignore_net_degree = 100;
    int   random_seed = -1;
    int   partition_fm_passes = 1;               // slot layout only; never changes the arithmetic
    float density_weight_init_multiplier = 0, density_weight_min_step = 0, density_weight_max_step = 0;
    float density_weight_worsening_hpwl_norm = -1.0f;
    int   adaptation_window = 0, density_jolt_interval = 1000;
    float slow_improvement_threshold = 0, high_overflow_threshold = 0;
    int   min_iterations = 0, max_iterations = 0, convergence_iterations = 30;
    float overflow_threshold = 0, aux_select_hpwl_ratio = 1.005f;
    bool  dump_schedule_trace = false;

    // [sw_flat] -- sw_flat-only knobs, all off by default: off, the run is bit-identical to sw_only.
    bool wa_exp_lut = false;              // wa_exp = "lut": the PL's exp table (hpwl_gradient_CU), not std::exp
    bool slot_order_deposit = false;      // slot_order has "deposit": density deposits (solver + overflow metric)
    bool slot_order_reductions = false;   // slot_order has "reductions": the per-node sums (BB, L1s, precond, lambda_0)
};
// Exits with a message on a missing required key or a config outside sw_flat's scope (D6). Meow.
PlacerConfig read_placer_config(const std::string& path);

enum class StopReason { RUNNING, CONVERGED, MAX_ITERATIONS, NAN_METRICS, NAN_PARTIALS, DIVERGED_HPWL, DIVERGENCE_GUARD };
const char* stop_reason_name(StopReason reason);

// Adds the scope's wall time to `total`. Meow.
struct PhaseTimer {
    double& total;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~PhaseTimer() { total += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); }
};

struct BestSolution {
    float hpwl = std::numeric_limits<float>::max();
    float overflow = std::numeric_limits<float>::max();
    int iteration = 0;
    bool valid = false;
};
enum BestSlot { PRIMARY, AUX, ROLLBACK };

// Node::State, split per field and per slot. Meow.
struct SlotState {
    std::vector<Position> node_pos;    // u
    std::vector<Position> probe_pos;   // v
    std::vector<Gradient> probe_grad;  // grad f(v): HPWL-only until combine_gradients, then total
    void resize(long n) { node_pos.assign(n, {}); probe_pos.assign(n, {}); probe_grad.assign(n, {}); }
};

struct Placer {
    PlacerConfig cfg;
    FlatDesign design;
    std::vector<char> movable_macro;   // per node
    Fillers fillers;
    SlotLayout layout;

    // sw_only's orders (see the file comment).
    std::vector<long> movable_slots;
    int filler_start = 0;              // movable_slots[filler_start..] are the fillers
    // The order each sum family walks: movable_slots (sw_only's), or ascending slots under
    // [sw_flat] slot_order (#45 step 5b). Components still precede fillers, so filler_start holds. Meow.
    std::vector<long> deposit_slots, reduce_slots;
    std::vector<int> fixed_nodes;
    std::vector<int> gradient_nets;    // file order, degree 2..ignore_net_degree
    std::vector<int> nets_by_name;     // name order, first of each name (std::map::emplace)
    std::vector<long> pin_slot;        // per pin: the node's OWNED slot, -1 = fixed node or IO pad

    // Per slot (only OWNED and FILLER slots are ever read).
    SlotState current, next;
    std::vector<Position> best_pos[3];
    std::vector<float> precond_weight, num_pins, area;
    std::vector<char> is_movable_macro;
    std::vector<Gradient> pin_partials;   // per pin, the ordered-replay buffer

    // Grid: bins_per_row x bins_per_row over [0, die), flat col * bins + row.
    int   bins = 0, die_w = 0, die_h = 0;   // die_w / die_h: Grid::getDieWidth, an int on purpose
    float bin_w = 0, bin_h = 0;
    std::vector<float> bin_area, bin_overlap;
    std::vector<float> fixed_base;     // the fixed nodes' deposit, capped: constant, so built once (density_map_CU's base map)
    std::vector<Gradient> efield;
    int   num_movable_macros = 0, formula_bins_per_row = 0;
    float total_movable_area = 0;

    // Schedule state, sw_only's members of the same names.
    float step_length = 0, density_weight = 1.0f, nesterov_ak = 1.0f, momentum_coeff = 0;
    float gamma = 0, inv_gamma = 0, base_gamma = 0;
    float precond_coef = 1.0f, precond_kappa = 0, precond_a1_norm = 0, precond_a2_norm = 0;
    float density_force_fraction = 0, last_gwl_L1 = 0, last_gden_L1 = 0;
    float last_pos_norm_sq = 0, last_grad_norm_sq = 0;
    int   iteration = 0, life = 30, convergence_iterations_remaining = -1, last_density_jolt_iter = -1000;
    bool  nan_detected = false, ever_converged = false;
    StopReason stop_reason = StopReason::RUNNING;
    std::vector<float> hpwl_history, ovfw_history;
    BestSolution best[3];

    // The PL's exp(-d/gamma) table (pl/src/modules/hpwl_gradient/wa_sums.hpp), for wa_exp = "lut". Meow.
    std::vector<float> exp_lut;

    // Wall time per phase, seconds, summed over the run (final.txt). Meow.
    double time_gradient = 0, time_field = 0, time_combine = 0, time_metrics = 0;

    // setup.cpp
    void setup(const PlacerConfig& config);
    void initialize_placement();
    void initialize_density_weight();
    // wirelength.cpp
    void compute_hpwl_partials();
    float total_wirelength(int max_net_degree, bool at_probe) const;
    // density.cpp
    void deposit_fixed_base();
    void compute_electric_fields();
    Gradient electrostatic_force(long slot) const;
    float compute_overflow(bool smooth) const;
    // step.cpp
    void perform_next_step();
    void estimate_initial_step();
    float lipschitz_estimate();
    void combine_gradients();
    void step_all_nodes();
    void enforce_die_boundaries(long slot);
    void cache_state();     // current = next, every movable slot
    void restore_state();   // next = current
    // schedule.cpp
    void update_precond_weights();
    void record_iteration_results();
    void update_schedule();
    bool check_convergence();
    int  select_best_solution() const;   // a BestSlot, or -1 = none valid
    // main.cpp
    void run();
    void perform_iteration();
};

} // namespace sw_flat

#endif // SW_FLAT_PLACER_HPP
