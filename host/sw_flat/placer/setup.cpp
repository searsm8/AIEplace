// setup.cpp -- sw_only's Setup.cpp on the flat design: config, the one parse, macro tags, fillers,
// the slot layout and sw_only's orders, the grid, the initial placement and the initial lambda. Meow.

#include "placer.hpp"
#include "flat_database.hpp"
#include "flat_netlist.hpp"
#include "format/density_footprint.hpp"   // densrec::RHO_WORD_BITS

#include "toml.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <numeric>
#include <random>

namespace sw_flat {

namespace {

constexpr long SLOT_CAPACITY = 1L << 20;   // pl/src/modules/hpwl_gradient/slot_arrays.hpp SLOT_CAPACITY

[[noreturn]] void fail(const std::string& why)
{
    fprintf(stderr, "sw_flat: %s\n", why.c_str());
    exit(1);
}

template <typename T> T require(const toml::table& cfg, const char* section, const char* key)
{
    if (auto value = cfg[section][key].value<T>()) return *value;
    fail(std::string("missing required config key [") + section + "] " + key);
}

} // namespace

PlacerConfig read_placer_config(const std::string& path)
{
    toml::table cfg;
    try { cfg = toml::parse_file(path); }
    catch (const toml::parse_error& err) { fail("cannot parse " + path + ": " + std::string(err.description())); }

    PlacerConfig c;
    c.benchmark = require<std::string>(cfg, "input", "benchmark");
    auto params = cfg["params"];
    c.init_gamma       = require<float>(cfg, "params", "init_gamma");
    c.gamma_schedule   = params["gamma_schedule"].value_or(c.gamma_schedule);
    c.gamma_bin_scaled = params["gamma_bin_scaled"].value_or(c.gamma_bin_scaled);
    c.gamma_ref_grid   = params["gamma_ref_grid"].value_or(c.gamma_ref_grid);
    c.init_step_seed   = params["init_step_seed"].value<float>().value_or(params["init_step_length"].value_or(0.01f));
    c.max_iterations   = require<int>(cfg, "params", "convergence_max_iterations");
    c.min_iterations   = require<int>(cfg, "params", "convergence_min_iterations");
    c.overflow_threshold = require<float>(cfg, "params", "convergence_overflow_threshold");
    c.aux_select_hpwl_ratio = params["aux_select_hpwl_ratio"].value_or(c.aux_select_hpwl_ratio);
    c.target_density   = params["maximum_utilization"].value_or(c.target_density);
    c.enable_momentum  = require<bool>(cfg, "params", "enable_momentum");
    c.enable_preconditioning  = params["enable_preconditioning"].value_or(c.enable_preconditioning);
    c.precond_coef_escalation = params["precond_coef_escalation"].value_or(c.precond_coef_escalation);
    c.enable_density_clamp    = params["enable_density_clamp"].value_or(c.enable_density_clamp);
    c.dct_normalize    = params["dct_normalize"].value_or(c.dct_normalize);
    c.convergence_iterations = params["convergence_iterations"].value_or(c.convergence_iterations);
    c.enable_filler    = require<bool>(cfg, "params", "enable_filler");
    c.bins_per_row     = params["bins_per_row"].value_or(0);
    c.ignore_net_degree = params["ignore_net_degree"].value_or(c.ignore_net_degree);
    c.random_seed      = params["random_seed"].value_or(c.random_seed);
    c.partition_fm_passes = params["partition_fm_passes"].value_or(c.partition_fm_passes);
    c.density_weight_init_multiplier = require<float>(cfg, "params", "density_weight_init_multiplier");
    c.density_weight_min_step = require<float>(cfg, "params", "density_weight_min_step");
    c.density_weight_max_step = require<float>(cfg, "params", "density_weight_max_step");
    c.density_weight_worsening_hpwl_norm = params["density_weight_worsening_hpwl_norm"].value_or(c.density_weight_worsening_hpwl_norm);
    c.adaptation_window = require<int>(cfg, "params", "adaptation_window");
    c.slow_improvement_threshold = require<float>(cfg, "params", "slow_improvement_threshold");
    c.high_overflow_threshold = require<float>(cfg, "params", "high_overflow_threshold");
    c.density_jolt_interval = params["density_jolt_interval"].value_or(c.density_jolt_interval);
    c.dump_schedule_trace = cfg["output"]["dump_schedule_trace"].value_or(false);

    // D6 scope: what sw_flat does not implement must not be silently ignored. Meow.
    if (require<bool>(cfg, "params", "enable_backtracking")) fail("enable_backtracking = true is outside sw_flat's scope (D6)");
    if (require<std::string>(cfg, "params", "partials_compute_method") != "cpu") fail("partials_compute_method must be \"cpu\"");
    if (require<std::string>(cfg, "params", "density_compute_method") != "cpu") fail("density_compute_method must be \"cpu\"");
    if (require<std::string>(cfg, "params", "wirelength_method") != "HPWL") fail("wirelength_method must be \"HPWL\"");
    if (!params["deterministic"].value_or(true)) fail("deterministic = false: sw_flat always sums in order");
    c.enable_phase2 = params["enable_phase2"].value_or(true);   // checked once the macros are tagged

    const std::string wa_exp = cfg["sw_flat"]["wa_exp"].value_or(std::string("exact"));
    if (wa_exp != "exact" && wa_exp != "lut") fail("[sw_flat] wa_exp must be \"exact\" or \"lut\"");
    c.wa_exp_lut = wa_exp == "lut";
    if (const toml::array* order = cfg["sw_flat"]["slot_order"].as_array())
        for (const auto& item : *order) {
            const std::string family = item.value_or(std::string());
            if (family == "deposit") c.slot_order_deposit = true;
            else if (family == "reductions") c.slot_order_reductions = true;
            else fail("[sw_flat] slot_order: unknown sum family \"" + family + "\" (deposit, reductions)");
        }
    const std::string fixed_base = cfg["sw_flat"]["fixed_base"].value_or(std::string("clamped"));
    if (fixed_base != "clamped" && fixed_base != "exact") fail("[sw_flat] fixed_base must be \"clamped\" or \"exact\"");
    c.fixed_base_exact = fixed_base == "exact";
    const std::string overflow_map = cfg["sw_flat"]["overflow_map"].value_or(std::string("separate"));
    if (overflow_map != "separate" && overflow_map != "snapshot") fail("[sw_flat] overflow_map must be \"separate\" or \"snapshot\"");
    c.overflow_snapshot = overflow_map == "snapshot";
    auto pick = [&](const char* key, const char* off, const char* on) {
        const std::string value = cfg["sw_flat"][key].value_or(std::string(off));
        if (value != off && value != on) fail(std::string("[sw_flat] ") + key + " must be "" + off + "" or "" + on + """);
        return value == on;
    };
    c.wa_sums_tree = pick("wa_sums", "serial", "tree");
    c.grad_merge_stream = pick("grad_merge", "file", "stream");
    c.hpwl_fixed = pick("hpwl", "float", "fixed");
    c.deposit_fixed = pick("deposit", "float", "fixed");
    c.deposit_frac_bits = cfg["sw_flat"]["deposit_frac_bits"].value_or(0);
    if (c.deposit_fixed && (c.deposit_frac_bits < 1 || c.deposit_frac_bits >= densrec::RHO_WORD_BITS))
        fail("[sw_flat] deposit = \"fixed\" needs 1 <= deposit_frac_bits < " + std::to_string(densrec::RHO_WORD_BITS));
    // The stream carries nets of 2..96 pins (the packer's IGNORE_NET_DEGREE): another mask would
    // compare two net sets, not two arithmetics. Meow.
    if (c.uses_record_stream() && c.ignore_net_degree != packer::IGNORE_NET_DEGREE)
        fail("[sw_flat] wa_sums / grad_merge / hpwl need ignore_net_degree = " + std::to_string(packer::IGNORE_NET_DEGREE));
    return c;
}

void Placer::setup(const PlacerConfig& config)
{
    cfg = config;
    {
        FlatDatabase flat_db;
        std::string why;
        if (!read_flat_design(cfg.benchmark, flat_db, why)) fail("cannot read " + cfg.benchmark + ": " + why);
        design = std::move(flat_db.design);
    }

    // loadDesignDatabase, tagMovableMacros, createFillers: the order matters, fillers may raise td. Meow.
    float target_density = effective_target_density(design, cfg.target_density);
    movable_macro = tag_movable_macros(design);
    fillers = Fillers{};
    fillers.target_density = target_density;
    if (cfg.enable_filler) fillers = make_fillers(design, movable_macro, target_density);
    cfg.target_density = fillers.target_density;
    num_movable_macros = (int)std::count(movable_macro.begin(), movable_macro.end(), 1);
    // sw_only would run phase 2 here (readyForPhase2); without movable macros it never does. Meow.
    if (num_movable_macros > 0 && cfg.enable_phase2) fail("movable macros with enable_phase2 = true: phase 2 is outside sw_flat's scope (D6)");

    packer::Config packer_cfg;
    packer_cfg.partition_fm_passes = cfg.partition_fm_passes;
    chunked = packer::encode_chunked(to_netlist(design, movable_macro, design.name), packer_cfg, SLOT_CAPACITY);
    if (!chunked.fits) fail("encode_chunked found no chunking");
    layout = build_slot_layout(design, chunked, fillers);

    // DataBase::buildNodeIndex's orders. IO pads are not components. Meow.
    std::vector<int> components;
    for (int node = 0; node < design.num_nodes(); node++)
        if (design.node_kind[node] != NodeKind::IO_PAD) components.push_back(node);
    std::sort(components.begin(), components.end(), [&](int a, int b) { return design.node_name[a] < design.node_name[b]; });
    double movable_sum = 0;
    for (int node : components) {
        if (design.node_kind[node] == NodeKind::FIXED) { fixed_nodes.push_back(node); continue; }
        if (layout.home_slot[node] < 0) fail("movable node " + design.node_name[node] + " has no slot");
        movable_slots.push_back(layout.home_slot[node]);
        movable_sum += design.node_width[node] * design.node_height[node];
    }
    total_movable_area = (float)movable_sum;   // DataBase::computeAreaBreakdown
    filler_start = (int)movable_slots.size();
    for (int filler = 0; filler < fillers.count; filler++) movable_slots.push_back(layout.first_filler_slot + filler);
    std::vector<long> ascending_slots = movable_slots;
    std::sort(ascending_slots.begin(), ascending_slots.end());   // the fillers' tail stays last
    deposit_slots = cfg.slot_order_deposit ? ascending_slots : movable_slots;
    reduce_slots = cfg.slot_order_reductions ? ascending_slots : movable_slots;

    // wa_sums.hpp's table: exp(-i * 0.05) for i < 12 / 0.05 + 2, read as exp(-d / gamma). Meow.
    for (int i = 0; i < (int)(PL_LUT_GAMMA_MULT / PL_LUT_STEP_NORM) + 2; i++) exp_lut.push_back(std::exp(-(float)i * PL_LUT_STEP_NORM));

    for (int net = 0; net < design.num_nets(); net++)
        if (design.net_degree(net) > 1 && design.net_degree(net) <= cfg.ignore_net_degree) gradient_nets.push_back(net);
    nets_by_name.resize(design.num_nets());
    std::iota(nets_by_name.begin(), nets_by_name.end(), 0);
    std::stable_sort(nets_by_name.begin(), nets_by_name.end(), [&](int a, int b) { return design.net_name[a] < design.net_name[b]; });
    nets_by_name.erase(std::unique(nets_by_name.begin(), nets_by_name.end(),
                                   [&](int a, int b) { return design.net_name[a] == design.net_name[b]; }),
                       nets_by_name.end());

    // Per slot: what sw_only's Node carries. num_pins is getNets().size(): one entry per pin, any degree. Meow.
    const long num_slots = layout.num_slots;
    current.resize(num_slots);
    next.resize(num_slots);
    for (auto& slot_pos : best_pos) slot_pos.assign(num_slots, {});
    precond_weight.assign(num_slots, 1.0f);
    area.assign(num_slots, 0.0f);
    is_movable_macro.assign(num_slots, 0);
    std::vector<int> pin_count(num_slots, 0);
    pin_slot.resize(design.pin_node.size());
    for (size_t pin = 0; pin < design.pin_node.size(); pin++) {
        pin_slot[pin] = layout.home_slot[design.pin_node[pin]];
        if (pin_slot[pin] >= 0) pin_count[pin_slot[pin]]++;
    }
    num_pins.assign(num_slots, 0.0f);
    for (long slot = 0; slot < num_slots; slot++) {
        num_pins[slot] = (float)pin_count[slot];
        area[slot] = layout.width[slot] * layout.height[slot];   // MacroClass::m_area
        if (layout.role[slot] == SlotRole::OWNED) is_movable_macro[slot] = movable_macro[layout.node[slot]];
    }
    pin_partials.assign(design.pin_node.size(), {});
    if (cfg.uses_record_stream()) build_record_stream();

    // analyzeDesignArea: float sums in name order; this fixed area is NOT clipped to the die. Meow.
    int movable_count = 0, movable_stdcell_count = 0;
    float movable_height_sum = 0.0f, fixed_area = 0.0f, movable_stdcell_area = 0.0f;
    for (int node : components) {
        const float width = design.node_width[node], height = design.node_height[node];
        if (design.node_kind[node] != NodeKind::FIXED) {
            movable_count++;
            movable_height_sum += height;
            float node_area = (float)width * height;
            if (!movable_macro[node]) { movable_stdcell_count++; movable_stdcell_area += node_area; }
        } else {
            fixed_area += width * height;
        }
    }
    const float avg_node_size = total_movable_area / std::max(1, movable_count);
    const float die_xsize = std::fabs(design.die_xl - design.die_xh);   // Box::getXsize
    const float die_ysize = std::fabs(design.die_yl - design.die_yh);
    const float placeable_area = std::max(1.0f, die_xsize * die_ysize - fixed_area);
    const float avg_grid_cell = (num_movable_macros > 0) ? movable_stdcell_area / std::max(1, movable_stdcell_count) : avg_node_size;
    const float total_bins = placeable_area * cfg.target_density / std::max(1.0f, avg_grid_cell);
    const int formula_bins = 1 << std::clamp((int)std::lround(std::log2(std::sqrt(total_bins))), 3, 12);
    const float row_height = movable_height_sum / std::max(1, movable_count);
    const int num_rows = (int)(die_ysize / std::max(1.0f, row_height));
    const int row_cap = 1 << std::clamp((int)std::floor(std::log2((float)std::max(1, num_rows))), 3, 12);
    formula_bins_per_row = std::min(formula_bins, row_cap);
    bins = cfg.bins_per_row > 0 ? std::min(cfg.bins_per_row, row_cap) : formula_bins_per_row;

    // setupGrid: bins start at 0, whatever the die's lower-left. Each bin's own Box area. Meow.
    die_w = (int)die_xsize;
    die_h = (int)die_ysize;
    bin_w = die_xsize / (float)bins;
    bin_h = die_ysize / (float)bins;
    bin_area.resize((size_t)bins * bins);
    for (int col = 0; col < bins; col++)
        for (int row = 0; row < bins; row++)
            bin_area[(size_t)col * bins + row] = std::fabs(col * bin_w - (col + 1) * bin_w) * std::fabs(row * bin_h - (row + 1) * bin_h);
    bin_overlap.assign((size_t)bins * bins, 0.0f);
    efield.assign((size_t)bins * bins, {});
    deposit_fixed_base();

    // configureGammaSchedule. Meow.
    base_gamma = cfg.init_gamma;
    if (cfg.gamma_bin_scaled) base_gamma = base_gamma * (die_w + die_h) / cfg.gamma_ref_grid;
    gamma = cfg.gamma_schedule ? 10.0f * base_gamma : base_gamma;
    inv_gamma = 1.0f / gamma;
    step_length = cfg.init_step_seed;
    density_weight = 1.0f;
}

// Gaussian cluster at the die centre for the cells, uniform for the fillers, then the expanded-size
// die bound once (XPlace trunc_node_pos_fn). No component of the 44 designs is PLACED, so every one
// is drawn; FlatDesign carries no PLACED flag. The draws use sw_only's exact expressions: the two
// gauss() / rand() calls inside one Position(...) are evaluated in the compiler's order, the same
// for both when the expression is the same. Meow.
void Placer::initialize_placement()
{
    Position target_pos = Position(die_w/2, die_h/2);
    int seed = cfg.random_seed;
    std::srand(seed >= 0 ? (unsigned)seed : (unsigned)std::time(nullptr));
    std::mt19937 gauss_gen(seed >= 0 ? (unsigned)seed : (unsigned)std::time(nullptr));
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    float gauss_sigma_x = die_w * 0.001f;
    float gauss_sigma_y = die_h * 0.001f;

    auto initialize_state = [&](long slot, Position pos) {
        current.node_pos[slot] = pos; current.probe_pos[slot] = pos;
        next.node_pos[slot] = pos;    next.probe_pos[slot] = pos;
    };
    for (int i = 0; i < filler_start; i++) {
        Position init_pos = target_pos + Position(gauss(gauss_gen) * gauss_sigma_x,
                                                   gauss(gauss_gen) * gauss_sigma_y);
        initialize_state(movable_slots[i], init_pos);
    }
    for (int i = filler_start; i < (int)movable_slots.size(); i++) {
        Position init_pos(rand() % die_w, rand() % die_h);
        initialize_state(movable_slots[i], init_pos);
    }
    for (long slot : movable_slots) {
        enforce_die_boundaries(slot);
        current.node_pos[slot] = next.node_pos[slot];
        current.probe_pos[slot] = next.probe_pos[slot];
        current.probe_grad[slot] = next.probe_grad[slot];
    }
    iteration = 0;
}

// lambda_0 = (sum |grad WL|_1 / sum |grad D|_1) * multiplier; fillers count toward the density norm. Meow.
void Placer::initialize_density_weight()
{
    float HPWL_L1_norm = 0.0f;
    float density_L1_norm = 0.0f;
    for (int i = 0; i < filler_start; i++) {
        const Gradient& partials = next.probe_grad[reduce_slots[i]];
        HPWL_L1_norm += std::fabs(partials.x) + std::fabs(partials.y);
        Gradient electro_force = electrostatic_force(reduce_slots[i]);
        density_L1_norm += std::fabs(electro_force.x) + std::fabs(electro_force.y);
    }
    for (int i = filler_start; i < (int)reduce_slots.size(); i++) {
        Gradient electro_force = electrostatic_force(reduce_slots[i]);
        density_L1_norm += std::fabs(electro_force.x) + std::fabs(electro_force.y);
    }
    density_weight = (HPWL_L1_norm / (density_L1_norm + 1e-8f)) * cfg.density_weight_init_multiplier;
    last_gwl_L1 = HPWL_L1_norm;
    last_gden_L1 = density_L1_norm;
}

} // namespace sw_flat
