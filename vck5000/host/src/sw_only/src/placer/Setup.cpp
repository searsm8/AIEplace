/**
 * @file Setup.cpp
 * @brief Pre-run setup: constructor-phase bring-up (config parse, LEF/DEF database read,
 *        area analysis, grid sizing, preconditioner setup) plus the one-time initial-placement
 *        and initial-density-weight steps run before iteration 1.
 *        Split out of AIEplace.cpp.
 */

#include "AIEplace.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <unistd.h> // isatty
#ifdef _OPENMP
#include <omp.h>
#endif

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief Pick the OpenMP team size: every logical CPU but one, unless OMP_NUM_THREADS says
 *        otherwise.
 *
 * Not a micro-optimization — it is insurance against sharing the machine, which on this box is
 * the normal case (overnight DSE/A-B sweeps). libgomp's default wait policy is a busy spin, so
 * the workers keep every CPU occupied through the serial stretches between parallel regions. A
 * team that already fills every CPU therefore has nowhere to put a co-scheduled job, and the
 * master ends up descheduled by its own idle workers. Measured with a loop of small parallel
 * regions separated by serial work, on this 8-vCPU box:
 *
 *     threads      1      2      4      6      7      8
 *     idle       0.68   0.68   0.69   0.71   0.72   0.73   s
 *     one other
 *     job running  -      -    0.70     -    0.73   5.89   s   <-- 8x
 *
 * Reserving one CPU costs nothing when the box is idle and avoids the collapse when it is not.
 * The placer itself showed the same thing: 8.1 -> 13.4 s on adaptec1 with a sweep running,
 * before this was in place.
 *
 * OMP_WAIT_POLICY=passive also avoids the collapse but is the wrong trade — it is ~20% SLOWER
 * on an idle box (0.88 s vs 0.72 s above) — and it cannot be set from here anyway: libgomp
 * reads its environment in a library constructor, so a setenv() in main() is already too late
 * (verified directly). The thread count is settable at runtime; the wait policy is not.
 *
 * An explicit OMP_NUM_THREADS always wins, so a concurrent sweep can still divide the box up —
 * and must: N runs x all-but-one-core each oversubscribes N-fold (tools/dse.py does this).
 */
static void configureThreadPool()
{
#ifdef _OPENMP
    if (getenv("OMP_NUM_THREADS")) {
        Logger::log_detail("OpenMP threads: " + std::to_string(omp_get_max_threads())
                           + " (from OMP_NUM_THREADS)");
        return;
    }
    int threads = std::max(1, omp_get_num_procs() - 1);
    omp_set_num_threads(threads);
    Logger::log_detail("OpenMP threads: " + std::to_string(threads) + " of "
                       + std::to_string(omp_get_num_procs()) + " CPUs (one reserved; see "
                       "configureThreadPool). Override with OMP_NUM_THREADS.");
#else
    Logger::log_detail("Built without OpenMP: placement runs single-threaded.");
#endif
}

/// @brief Config parse + grid decision + DB read + fillers + area analysis, timed as one unit.
void Placer::setupDesign()
{
    TIME_FUNCTION();
    loadConfiguration();
    configureThreadPool();
    bool bins_auto = resolveGridResolution();
    loadDesignDatabase();
    tagMovableMacros();           // must precede createFillers: the filler math is std-cell-only
    createFillers();              // may raise target_density; then builds the flat node index
    analyzeDesignArea(bins_auto);
    configurePreconditioner();
    applyMixedSizeStopPolicy();   // needs num_movable_macros from analyzeDesignArea
}

void Placer::setupGrid()
{
    grid = Grid(db.getDieArea(), bins_per_row, bins_per_row);
    grid.setClampDensity(enable_density_clamp);
    grid.setTargetDensity(target_density);
    die_size = min(grid.getDieWidth(), grid.getDieHeight());
}

/**
 * @brief Tag movable macros with XPlace's is_mov_macro rule (database.py:621-632). Consumed by
 *        DataBase::addFillers (which works in the standard-cell frame), the macro deposit weight
 *        in computeNodeFootprint (TODO #11b), and the macro legalizer. A MOVABLE node is a macro
 *        iff all three hold:
 *          1. height > 2.01 * row_height          (taller than ~two standard-cell rows)
 *          2. area   > 10 * mean(area of the smallest 99.9% of movable nodes)
 *          3. both dimensions non-degenerate
 *        Fillers are excluded (XPlace's masked_fill spans only the real movable range), as are
 *        FIXED nodes (XPlace clears is_mov_macro above mov_rhs).
 *
 * row_height uses the mean movable cell height — the same proxy analyzeDesignArea already uses for
 * the ePlace grid formula; sw_only has no parsed row pitch exposed here.
 *
 * NOTE: this is a finer rule than the die-area heuristic behind Placer::num_movable_macros (which
 * drives the auto-preconditioner). Two macro definitions now coexist — unify later, see TODO #11.
 */
void Placer::tagMovableMacros()
{
    std::vector<float> movable_areas;
    float height_sum = 0.0f;
    for (const auto& item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        movable_areas.push_back(item.second->getArea());
        height_sum += item.second->getYsize();
    }
    if (movable_areas.empty()) return;

    float row_height = height_sum / movable_areas.size();

    // Threshold from the smallest 99.9% of movable areas, so a handful of huge macros cannot drag
    // the mean up and hide themselves.
    std::vector<float> ascending_areas = movable_areas;
    std::sort(ascending_areas.begin(), ascending_areas.end());
    size_t small_count = std::max<size_t>(1, (size_t)(ascending_areas.size() * 0.999));
    float mean_small_area = std::accumulate(ascending_areas.begin(),
                                            ascending_areas.begin() + small_count, 0.0f) / small_count;
    float macro_area_threshold = 10.0f * mean_small_area;

    int tagged = 0;
    for (const auto& item : db.getComponents()) {
        Component* comp_p = item.second;
        if (comp_p->getStatus() == FIXED) continue;
        bool is_tall  = comp_p->getYsize() > 2.01f * row_height;
        bool is_large = comp_p->getArea() > macro_area_threshold;
        bool is_sized = comp_p->getXsize() > 1e-4f && comp_p->getYsize() > 1e-4f;
        comp_p->setMovableMacro(is_tall && is_large && is_sized);
        if (comp_p->isMovableMacro()) tagged++;
    }
    Logger::log_detail("Movable macros (XPlace is_mov_macro rule): " + std::to_string(tagged)
        + "  [row_height=" + PREC(row_height) + ", area_thresh=" + SCI(macro_area_threshold) + "]");
}

/**
 * @brief Decide bins_per_row from an explicit config override; otherwise defer to the
 *        ePlace-formula grid computed once the database is read (see analyzeDesignArea).
 * @return true if the grid still needs to be auto-sized (no explicit override was given)
 */
bool Placer::resolveGridResolution()
{
    bool bins_auto = !bool(cfg["params"]["bins_per_row"]);
    if (!bins_auto) {
        bins_per_row = cfg["params"]["bins_per_row"].value_or(bins_per_row);
        Logger::log_info("Grid resolution: " + std::to_string(bins_per_row) + " x " + std::to_string(bins_per_row));
    }
    return bins_auto;
}

/// @brief Read the LEF/DEF design files and apply the benchmark's maximum_utilization if given.
void Placer::loadDesignDatabase()
{
    db = DataBase(input_dir); // TODO: Database initialization should be multithreaded?

    // Benchmark-specified maximum_utilization overrides config default
    if (db.getMaximumUtilization() > 0.0f) {
        target_density = db.getMaximumUtilization();
        Logger::log_info("Using benchmark maximum_utilization: " +
                        std::to_string(target_density));
    }
}

/**
 * @brief Size and create the filler cells, then build the flat node index.
 *
 * Runs after tagMovableMacros because the filler math is standard-cell-only, and before
 * analyzeDesignArea because addFillers may RAISE target_density (XPlace does the same when a
 * design is denser than its target) and the grid formula reads it.
 */
void Placer::createFillers()
{
    if (ConfigUtils::require<bool>(cfg, "params", "enable_filler"))
        target_density = db.addFillers(target_density);

    // Every node now exists and every PlacementStatus is final, so the flat iteration index
    // the threaded loops walk can be built once here.
    db.buildNodeIndex();
}

/**
 * @brief Parse the TOML config file named by m_config_filepath into cfg (toml++), then read
 *        every hyperparameter, compute-method, and convergence setting.
 *        gamma/base_gamma are only seeded here; they are finalized once the grid exists.
 */
void Placer::loadConfiguration()
{
    // Read configuration file
    std::ifstream config_file(m_config_filepath);
    // check if config file was found
    if (!config_file.is_open()) {
        Logger::log_error("Unable to open configuration file: " + m_config_filepath);
        exit(1);
    }

    pgrm_start_time = getTime();

    std::stringstream buffer;
    buffer << config_file.rdbuf();
    config_file.close();

    // Parse TOML
    try {
        cfg = toml::parse(buffer.str(), m_config_filepath);
    } catch (const toml::parse_error& err) {
        Logger::log_error("Failed to parse configuration file: " + m_config_filepath
            + "\n" + std::string(err.description()));
        exit(1);
    }

    // Console verbosity. `interactive` follows the stream unless the config forces it: a terminal
    // gets the banner and the live-status table, a pipe (DSE sweep, nohup log) gets neither.
    // Either way the full-detail run report is written to the run directory (see openReport).
    quiet = cfg["output"]["quiet"].value_or(quiet);
    interactive = cfg["output"]["interactive"].value_or(isatty(fileno(stdout)) != 0);
    Logger::setup_logging(quiet       ? LogLevel::ERROR   // errors only
                          : interactive ? LogLevel::ITER  // + per-iteration live status
                                        : LogLevel::INFO);
    printWelcomeBanner();
    Logger::log_info("Reading runtime configuration from: " + m_config_filepath);

    // Read hyperparameters. gamma/base_gamma are finalized after the grid is built
    // (gamma_bin_scaled ties base_gamma to the bin geometry — see after grid creation).
    // The .value_or() fallbacks below reference the member itself, so each configurable
    // default is defined ONCE — at the member's header initializer (single source of truth).
    base_gamma    = ConfigUtils::require<float>(cfg, "params", "init_gamma");
    gamma_schedule = cfg["params"]["gamma_schedule"].value_or(gamma_schedule);
    gamma_bin_scaled = cfg["params"]["gamma_bin_scaled"].value_or(gamma_bin_scaled);
    gamma_ref_grid   = cfg["params"]["gamma_ref_grid"].value_or(gamma_ref_grid);
    // init_step_seed: BB trial-step SEED for estimateInitialStep(), in SITE WIDTHS — the unit
    // XPlace's args.lr (default 0.01) is in, since XPlace prescales every coordinate by site width.
    // Not the literal first step: the real iteration-1 α is calibrated by the Barzilai-Borwein
    // estimate. Back-compat: accept the old init_step_length key if present.
    init_step_seed = cfg["params"]["init_step_seed"].value<float>()
                   .value_or(cfg["params"]["init_step_length"].value_or(0.01f));
    if (!cfg["params"]["init_step_seed"] && cfg["params"]["init_step_length"])
        Logger::log_warning("Config uses deprecated 'init_step_length'; treat as 'init_step_seed'.");
    step_length = init_step_seed; // placeholder; estimateInitialStep() overwrites on iteration 1
    density_weight = 1.0f; // will be updated on iteration 1 after computing gradients

    // Read compute methods
    partials_method = ConfigUtils::require<std::string>(cfg, "params", "partials_compute_method");
    density_method = ConfigUtils::require<std::string>(cfg, "params", "density_compute_method");
    Logger::log_detail("Partials compute method: " + partials_method);
    Logger::log_detail("Density compute method:  " + density_method);

    // Read Convergence criteria
    max_iterations = ConfigUtils::require<int>(cfg, "params", "convergence_max_iterations");
    min_iterations = ConfigUtils::require<int>(cfg, "params", "convergence_min_iterations");
    hpwl_improvement_threshold = ConfigUtils::require<float>(cfg, "params", "convergence_hpwl_improvement_threshold");
    overflow_threshold = ConfigUtils::require<float>(cfg, "params", "convergence_overflow_threshold");
    // value_or, not require: the frozen test/regress configs predate this key and must keep working.
    best_aux_max_hpwl_ratio = cfg["params"]["best_aux_max_hpwl_ratio"].value_or(best_aux_max_hpwl_ratio);
    target_density = cfg["params"]["maximum_utilization"].value_or(target_density);
    enable_backtracking = ConfigUtils::require<bool>(cfg, "params", "enable_backtracking");
    enable_momentum = ConfigUtils::require<bool>(cfg, "params", "enable_momentum");
    enable_preconditioning = cfg["params"]["enable_preconditioning"].value_or(enable_preconditioning);
    precond_coef_escalation = cfg["params"]["precond_coef_escalation"].value_or(precond_coef_escalation);
    enable_density_clamp = cfg["params"]["enable_density_clamp"].value_or(enable_density_clamp);
    // TODO #13 phase 2 (see Phase2.cpp). Only ever engages on a design with movable macros.
    enable_phase2 = cfg["params"]["enable_phase2"].value_or(enable_phase2);
    macro_legalization_enabled =
        cfg["params"]["macro_legalization"].value_or(macro_legalization_enabled);
    macro_lp_solver = cfg["params"]["macro_lp_solver"].value_or(std::string(""));
    dct_normalize = cfg["params"]["dct_normalize"].value_or(dct_normalize);
    g_deterministic = cfg["params"]["deterministic"].value_or(g_deterministic); // see Common.h
    convergence_window = ConfigUtils::require<int>(cfg, "params", "convergence_window");
    convergence_iterations = cfg["params"]["convergence_iterations"].value_or(convergence_iterations);
    max_backtracking_attempts = ConfigUtils::require<int>(cfg, "params", "backtrack_max_tries");
    backtrack_epsilon = ConfigUtils::require<float>(cfg, "params", "backtrack_epsilon");

    // Read other stuff
    input_dir = fs::path(ConfigUtils::require<std::string>(cfg, "input", "benchmark"));
    results_dir = fs::path(ConfigUtils::require<std::string>(cfg, "output", "results_dir"));
}

/**
 * @brief Scan the design's components: derive the preconditioner's area normalization
 *        (avg_node_size), detect movable macros (num_movable_macros), and compute the
 *        ePlace-formula grid size (formula_bins_per_row) — applied to bins_per_row when
 *        @p bins_auto (no explicit override was given).
 * @param bins_auto result of resolveGridResolution(): true if the grid wasn't pinned by config
 */
void Placer::analyzeDesignArea(bool bins_auto)
{
    // Preconditioner area normalization: average cell area so area term is O(1) for standard cells
    // (XPlace achieves this by normalizing all coordinates by site_width)
    int movable_count = 0, movable_stdcell_count = 0;
    float movable_height_sum = 0.0f, fixed_area = 0.0f, movable_stdcell_area = 0.0f;
    // Macro classification is Node::isMovableMacro() — the single definition, set by
    // tagMovableMacros() from XPlace's is_mov_macro rule, which runs before this. This used to
    // apply its own "area > 0.02% of the die" threshold, a second rule that disagreed with the
    // tag on 7 of 16 MMS designs (worst newblue1, 64 vs 53). Unifying is behaviour-preserving
    // in practice: every *functional* use of num_movable_macros below is a `> 0` test, both
    // rules are non-zero on every design with macros, and the ePlace grid the two produce is
    // identical on all 16 (the power-of-2 rounding absorbs the difference).
    for (auto& item : db.getComponents())
        if (item.second->getStatus() != FIXED) {
            movable_count++;
            movable_height_sum += item.second->getYsize();
            float node_area = (float)item.second->getXsize() * item.second->getYsize();
            if (item.second->isMovableMacro()) {
                num_movable_macros++;
            } else {
                movable_stdcell_count++;
                movable_stdcell_area += node_area;
            }
        } else {
            fixed_area += item.second->getXsize() * item.second->getYsize();
        }
    avg_node_size = db.getTotalMovableArea() / std::max(1, movable_count);

    // ePlace grid sizing (paper: |B| = V_R * target_density / (k * avg_cell_area), k=1 =>
    // ~one movable cell per bin). V_R is the placement region = die minus fixed blocks;
    // with target_density = util = movable_area/V_R this gives |B| ~ N_movable. Round to a
    // power of 2 (FFT needs it), then cap so a bin is never shorter than a standard-cell row
    // (bin_height >= row_height), mirroring XPlace's num_bin <= num_rows guard. Only applied
    // when the grid was not pinned by config (bins_auto).
    // Computed ALWAYS (recorded in run_summary even when an explicit bins_per_row overrides it,
    // so the grid-sizing sweep can compare formula vs best grid).
    float placeable_area = std::max(1.0f, db.getDieArea().getArea() - fixed_area);
    // Grid divisor uses the average STD-CELL area, not the all-movable average: big
    // movable macros inflate the mean and coarsen the grid, which under-reads density on
    // macro-heavy designs (adaptec2: 127 macros -> 512 grid stopped early at true-ovfw
    // 0.087; excluding macros -> 1024 grid, ovfw 0.057, legalizes -0.3% vs XPlace instead
    // of a +17% blowup). Bit-identical to the old formula when there are no movable macros.
    float avg_grid_cell = (num_movable_macros > 0)
        ? movable_stdcell_area / std::max(1, movable_stdcell_count)
        : avg_node_size; // no macros -> exactly the old divisor (bit-identical)
    float total_bins = placeable_area * target_density / std::max(1.0f, avg_grid_cell);
    int   bins       = 1 << std::clamp((int)std::lround(std::log2(std::sqrt(total_bins))), 3, 12);
    float row_height = movable_height_sum / std::max(1, movable_count);
    int   num_rows   = (int)(db.getDieArea().getYsize() / std::max(1.0f, row_height));
    int   row_cap    = 1 << std::clamp((int)std::floor(std::log2((float)std::max(1, num_rows))), 3, 12);
    formula_bins_per_row = std::min(bins, row_cap);
    if (bins_auto) {
        bins_per_row = formula_bins_per_row;
    } else if (bins_per_row > row_cap) {
        // CLAUDE CODE: XPlace caps an explicitly-REQUESTED grid at num_rows too (database.py:161),
        // not just its auto formula. A bin shorter than a standard-cell row over-resolves the
        // density field and manufactures overflow -- worse at target_density<1, which caps only
        // fixed/macro density, never std cells. This is what made our exact overflow read ~7x
        // XPlace's on the low-row mgc designs (they run capped there, we ran the raw 512). Meow.
        Logger::log_info("Grid override " + std::to_string(bins_per_row) + " exceeds num_rows "
            + std::to_string(num_rows) + "; capping to " + std::to_string(row_cap)
            + " (XPlace database.py:161)");
        bins_per_row = row_cap;
    }
    Logger::log_info("Grid (ePlace formula): " + std::to_string(formula_bins_per_row)
        + "  [sqrt|B|=" + std::to_string(bins) + ", num_rows=" + std::to_string(num_rows)
        + ", row_cap=" + std::to_string(row_cap) + "]  effective bins_per_row="
        + std::to_string(bins_per_row));
}

/**
 * @brief The preconditioner is ON for every design, matching XPlace (`--use_precond` defaults to
 *        True, main.py:35, and nothing in its flow turns it off). A config value can force it OFF,
 *        which is a diagnostic only -- see the trap noted in updatePrecondCoef.
 *
 * ### Why the movable-macro auto-rule was removed — 2026-08-11
 * `enable_preconditioning` was a flat `false` until 638b9a8 (2026-07-17), which made it auto-ON iff
 * `num_movable_macros > 0` on the evidence that preconditioning was "a wash on fixed-macro designs".
 * True of the preconditioner itself; false of the flag, because precond_coef ALSO feeds
 * precond_kappa, which gates the gamma/lambda throttle for every design (see updatePrecondCoef).
 * Zero movable macros meant the whole ISPD tier -- 28 of 28 designs -- ran with the throttle's only
 * release mechanism frozen at precond_coef = 1.0. bigblue3 is where it finally cost a run: lambda
 * stalled at 1/3 rate, overflow flattened at 0.18, and the plateau guard killed it at iteration 678
 * for +5.65% HPWL vs XPlace. Forcing this ON: 798 iterations, converged, +1.15%.
 *
 * The lesson generalises past this flag: a switch named for one mechanism silently gated a second.
 */
void Placer::configurePreconditioner()
{
    Logger::log_info("Movable macros detected: " + std::to_string(num_movable_macros)
        + " (preconditioner " + std::string(enable_preconditioning ? "ON" : "OFF") + ")");
}

/**
 * @brief Mixed-size mode: this design places movable macros and standard cells SIMULTANEOUSLY,
 *        which is XPlace's `include_macros` phase (param_scheduler.py set_mixsize_init_param,
 *        `enable_mixed_size and not zero_macro_grad`). sw_only implements only that phase — it has
 *        no macro-legalization + fixed-macro second pass — so the flag is simply "has movable
 *        macros". XPlace loosens two stop rules while it holds; see applyMixedSizeStopPolicy.
 */
void Placer::applyMixedSizeStopPolicy()
{
    mixed_size_mode = (num_movable_macros > 0);
    if (!mixed_size_mode) return;

    // XPlace ALSO does `self.stop_overflow = args.stop_overflow * 2.0` here — its Mixed-GP target
    // is 0.14, not 0.07, which is why its phase 1 ends at 0.10-0.18 exact overflow. We do NOT
    // apply it yet, and the reason is that it is HALF of an inseparable pair.
    //
    // Doubling stop_overflow also doubles the guard-arm band (`overflow < stop_overflow * 5`,
    // 0.35 -> 0.70). XPlace can afford that only because it simultaneously disables the
    // overflow-plateau kill in this phase (`not include_macros`). We must keep that kill active —
    // it is our only phase-1 exit until TODO #13 adds macro legalization + the fixed-macro pass.
    // Applying the doubling alone (tried 2026-07-31) arms the guard during the EARLY plateau every
    // design has before the density weight ramps: newblue1 died at iteration 258 with overflow
    // still 0.70 and HPWL 2.39e7 vs 6.15e7 — an unspread placement.
    //
    // Re-enable BOTH together with #13. `mixed_size_mode` is already wired up for that.
    Logger::log_info("Mixed-size mode: " + std::to_string(num_movable_macros)
        + " movable macros (XPlace include_macros phase; stop overflow stays at "
        + PREC(overflow_threshold) + " until TODO #13 adds phase 2).");
}

/**
 * @brief Initialize placement of all movable nodes: a tight Gaussian cluster at the die center
 *        (XPlace-style; per-axis sigma = 0.001 * die span), relying on the density force to
 *        spread the near-coincident cells over early iterations. Benchmark-provided (PLACED)
 *        positions are kept as-is.
 */
void Placer::initializePlacement()
{
    Logger::log_trace("Begin initializePlacement()");
    Position target_pos = Position(grid.getDieWidth()/2, grid.getDieHeight()/2); // die center

    // Fixed seed (random_seed >= 0) gives identical initial placement across runs — needed for
    // controlled A/B tests; default -1 keeps the time-based seed.
    int seed = cfg["params"]["random_seed"].value_or(-1);
    std::srand(seed >= 0 ? (unsigned)seed : (unsigned)std::time(nullptr));
    std::mt19937 gauss_gen(seed >= 0 ? (unsigned)seed : (unsigned)std::time(nullptr));
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    float gauss_sigma_x = grid.getDieWidth() * 0.001f;
    float gauss_sigma_y = grid.getDieHeight() * 0.001f;

    int placed_count = 0, randomized_count = 0;

    for (const auto& item : db.getComponents()) {
        Component* comp_p = item.second;

        // If components are already tagged as PLACED or FIXED, leave them alone
        if (comp_p->getStatus() == PLACED || comp_p->getStatus() == FIXED) {
            comp_p->initializeState(comp_p->next.node_pos);
            placed_count++;
        } else {
            Position init_pos = target_pos + Position(gauss(gauss_gen) * gauss_sigma_x,
                                                       gauss(gauss_gen) * gauss_sigma_y);
            comp_p->initializeState(init_pos);
            randomized_count++;
        }
    }
    Logger::log_detail("Initial placement: " + std::to_string(placed_count) +
                     " from benchmark, " + std::to_string(randomized_count) + " randomized");

    // Place Fillers uniformly at random across the whole die (XPlace get_filler_pos):
    // fillers must represent whitespace everywhere, so unlike the real cells they are NOT
    // clustered at the center — spreading them seeds the density model with the vacant
    // regions that the real cells should eventually flow into.
    for (auto filler_p : db.getFillers()) {
        Position init_pos(rand() % grid.getDieWidth(), rand() % grid.getDieHeight());
        filler_p->initializeState(init_pos);
    }

    // Initialize IO pad state (fixed, but need valid probe_pos for gradient computation)
    for (const auto& item : db.getIOPads())
        item.second->initializeState(item.second->next.node_pos);


    // XPlace applies trunc_node_pos_fn once before the first gradient (initializer.py init_params).
    // It matters here because fillers are seeded uniformly at random, so some start with their
    // expanded footprint hanging off the die.
    for (const auto& item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        enforceDieBoundaries(item.second);
        item.second->cacheState();   // resync current with the projected next
    }
    for (auto filler_p : db.getFillers()) {
        enforceDieBoundaries(filler_p);
        filler_p->cacheState();
    }

    //printIterationResults(); // Prints "iteration 0" starting statistics
    iteration = 0;

    // These macros won't be on any nets, but they will add to the density computation
    // and could be created en masse at hotspot areas to gently push other nodes away.
}

/**
 * @brief Set initial density_weight based on ratio of total HPWL gradient to density gradient
 * Compute total force produced by the HPWL gradient,
 * and the total force produced by the density gradient.
 * Set initial density_weight as the ratio between these two to make them roughly balanced,
 * multiplied by a small number which makes the density force initially weaker
 */
void Placer::initializeDensityWeight()
{
    float HPWL_L1_norm = 0.0f;
    float density_L1_norm = 0.0f;
    for(const auto& item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* node_p = item.second;

        Gradient& partials = node_p->next.probe_grad;  // HPWL-only (before combineGradients)
        HPWL_L1_norm += fabs(partials.x) + fabs(partials.y);

        Gradient electro_force = computeElectrostaticForce(node_p);
        density_L1_norm += fabs(electro_force.x) + fabs(electro_force.y);
    }
    // Fillers carry density force but no wirelength; XPlace's init balance counts them.
    for (auto filler_p : db.getFillers()) {
        Gradient electro_force = computeElectrostaticForce(filler_p);
        density_L1_norm += fabs(electro_force.x) + fabs(electro_force.y);
    }

    float initial_multiplier = ConfigUtils::require<float>(cfg, "params", "density_weight_init_multiplier");
    density_weight = (HPWL_L1_norm / (density_L1_norm + 1e-8f)) * initial_multiplier;
    last_gwl_L1  = HPWL_L1_norm;    // seeds density_force_fraction for iteration 1
    last_gden_L1 = density_L1_norm;

    Logger::log_detail("Initial HPWL gradient L1 norm: " + std::to_string(HPWL_L1_norm));
    Logger::log_detail("Initial density gradient L1 norm: " + std::to_string(density_L1_norm));
    Logger::log_detail("Initialized density_weight: " + std::to_string(density_weight));
}

AIEPLACE_NAMESPACE_END
