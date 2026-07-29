#include "DCT.h"
#include "AIEplace.h"
#include "JsonUtils.h"
#include <cmath>
#include <cassert>
#include <random>

AIEPLACE_NAMESPACE_BEGIN

using namespace tabulate; // table types for clean display, scoped to this .cpp (not leaked via Logger.h)

/**
 * @brief Run the ePlace algorithm
 *
 * Initialize the placement, then perform iterations
 * until the convergence condition is met.
 */
void Placer::run()
{
    initializePlacement();
    while( true )
    {
        performIteration();
        if(checkConvergence() || m_diverged )
            break;
    }
}

/**
 * @brief Run one placement iteration: compute the wirelength and density gradients, combine
 *        them, take a Barzilai-Borwein (optionally backtracked) Nesterov step, then update the
 *        gamma/lambda schedule and best-solution tracking.
 */
void Placer::performIteration()
{
    TIME_FUNCTION();

    Logger::log_detail("BEGIN iteration " + std::to_string(iteration));
    ++iteration;

    if (iteration == 1)
        initializeFirstIteration();

    updatePrecondWeights();

    if (iteration == 1)
        estimateInitialStep();

    performNextStep(enable_backtracking);

    recordIterationResults();
    printIterationResults();

    updateSchedule();

    if (m_diverged)
        Logger::log_error("Stopping: NaN in HPWL partials at iteration " +
                          std::to_string(iteration) + " (hard divergence)");
}

/**
 * @brief Iteration-1 bootstrap: compute the first gradients and initialize solver state.
 *        Probe positions (v_1 = u_1) are already set by initializePlacement().
 */
void Placer::initializeFirstIteration()
{
    iterationReset();

    computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
    if (compare_hpwl_methods) compareHpwlPartials();
    computeElectricFields();    // ∇D from ρ → bin eFields

    initializeDensityWeight();
    recordInitialHPWL();
}

Placer::Placer(std::string config_filepath)
{
    m_config_filepath = config_filepath;

    setupDesign();
    Logger::log_info("Database setup time: " + std::to_string(Logger::getFunctionTime("setupDesign") / 1.0e6) + " s");

    setupGrid();
    createRunOutputStructure();
    configureGammaSchedule();
    initializeVisualization();
}

/// @brief Config parse + grid decision + DB read + fillers + area analysis, timed as one unit.
void Placer::setupDesign()
{
    TIME_FUNCTION();
    loadConfiguration();
    bool bins_auto = resolveGridResolution();
    loadDesignDatabase();
    analyzeDesignArea(bins_auto);
    configurePreconditioner();
}

void Placer::setupGrid()
{
    grid = Grid(db.getDieArea(), bins_per_row, bins_per_row);
    grid.setClampDensity(enable_density_clamp);
    die_size = min(grid.getDieWidth(), grid.getDieHeight());
}

/**
 * @brief Decide bins_per_row from an explicit config override; otherwise defer to the
 *        ePlace-formula grid computed once the database is read (see analyzeDesignArea).
 * @return true if the grid still needs to be auto-sized (no explicit override was given)
 */
bool Placer::resolveGridResolution()
{
    bool bins_auto = !cfg["params"].contains("bins_per_row");
    if (!bins_auto) {
        bins_per_row = cfg["params"]["bins_per_row"];
        Logger::log_info("Grid resolution: " + std::to_string(bins_per_row) + " x " + std::to_string(bins_per_row));
    }
    return bins_auto;
}

/// @brief Read the LEF/DEF design files, apply the benchmark's maximum_utilization if given, add fillers.
void Placer::loadDesignDatabase()
{
    db = DataBase(input_dir); // TODO: Database initialization should be multithreaded?

    // Benchmark-specified maximum_utilization overrides config default
    if (db.getMaximumUtilization() > 0.0f) {
        target_density = db.getMaximumUtilization();
        Logger::log_info("Using benchmark maximum_utilization: " +
                        std::to_string(target_density));
    }

    if (cfg["params"]["enable_filler"])
        db.addFillers(target_density);
}


/**
 * @brief Constructor. initialize a new Placer object and the placement system
 * Initialize a new Placer object by reading JSON configuration file.
 * Initializes the database from LEF/DEF files. (See DataBase.cpp constructor)
 * Initialize the grid structure based on size of benchmark.
 * If hardware acceleration is enabled, initializes XRT/AIE drivers.
 *
 * @param config_filepath Path to the JSON configuration file
 */
/**
 * @brief Parse the (comment-stripped) JSON config file named by m_config_filepath into
 *        cfg, then read every hyperparameter, compute-method, and convergence setting.
 *        gamma/base_gamma are only seeded here; they are finalized once the grid exists.
 */
void Placer::loadConfiguration()
{
    // Read configuration file (supports JSON with comments)
    std::ifstream config_file(m_config_filepath);
    // check if config file was found
    if (!config_file.is_open()) {
        Logger::log_error("Unable to open configuration file: " + m_config_filepath);
        exit(1);
    }

    pgrm_start_time = getTime();

    // Read file content and strip comments
    std::stringstream buffer;
    buffer << config_file.rdbuf();
    config_file.close();
    std::string config_content = buffer.str();
    std::string json_content = JsonUtils::stripComments(config_content);

    // Parse JSON
    cfg = json::parse(json_content);

    // Setup logging (quiet mode suppresses all output except errors)
    quiet = cfg["output"].value("quiet", quiet);
    Logger::setup_logging(quiet);
    printWelcomeBanner();
    Logger::log_info("Reading runtime configuration from: " + m_config_filepath);

    // Read hyperparameters. gamma/base_gamma are finalized after the grid is built
    // (gamma_bin_scaled ties base_gamma to the bin geometry — see after grid creation).
    // The .value() fallbacks below reference the member itself, so each configurable
    // default is defined ONCE — at the member's header initializer (single source of truth).
    base_gamma    = cfg["params"]["init_gamma"];
    gamma_schedule = cfg["params"].value("gamma_schedule", gamma_schedule);
    gamma_bin_scaled = cfg["params"].value("gamma_bin_scaled", gamma_bin_scaled);
    gamma_ref_grid   = cfg["params"].value("gamma_ref_grid", gamma_ref_grid);
    // init_step_seed: BB trial-step SEED for estimateInitialStep() (XPlace args.lr, default
    // 0.01) — not the literal first step. The real iteration-1 α is calibrated by the
    // Barzilai-Borwein estimate. Back-compat: accept the old init_step_length key if present.
    init_step_seed = cfg["params"].contains("init_step_seed")
                   ? cfg["params"]["init_step_seed"].get<float>()
                   : cfg["params"].value("init_step_length", 0.01f);
    if (!cfg["params"].contains("init_step_seed") && cfg["params"].contains("init_step_length"))
        Logger::log_info("Config uses deprecated 'init_step_length'; treat as 'init_step_seed'.");
    step_length = init_step_seed; // placeholder; estimateInitialStep() overwrites on iteration 1
    density_weight = 1.0f; // will be updated on iteration 1 after computing gradients

    // Read compute methods
    partials_method = cfg["params"]["partials_compute_method"];
    density_method = cfg["params"]["density_compute_method"];
    Logger::log_info("Partials compute method: " + partials_method);
    Logger::log_info("Density compute method:  " + density_method);

    // Read Convergence criteria
    max_iterations = cfg["params"]["convergence_max_iterations"];
    min_iterations = cfg["params"]["convergence_min_iterations"];
    hpwl_improvement_threshold = cfg["params"]["convergence_hpwl_improvement_threshold"];
    overflow_threshold = cfg["params"]["convergence_overflow_threshold"];
    target_density = cfg["params"].value("maximum_utilization", target_density);
    enable_backtracking = cfg["params"]["enable_backtracking"];
    enable_momentum = cfg["params"]["enable_momentum"];
    precond_explicitly_set = cfg["params"].contains("enable_preconditioning");
    enable_preconditioning = cfg["params"].value("enable_preconditioning", enable_preconditioning);
    auto_enable_preconditioning = cfg["params"].value("auto_enable_preconditioning", auto_enable_preconditioning);
    precond_coef_escalation = cfg["params"].value("precond_coef_escalation", precond_coef_escalation);
    enable_density_clamp = cfg["params"].value("enable_density_clamp", enable_density_clamp);
    dct_normalize = cfg["params"].value("dct_normalize", dct_normalize);
    convergence_window = cfg["params"]["convergence_window"];
    convergence_iterations = cfg["params"].value("convergence_iterations", convergence_iterations);
    max_backtracking_attempts = cfg["params"]["backtrack_max_tries"];
    backtrack_epsilon = cfg["params"]["backtrack_epsilon"];

    // Read other stuff
    compare_hpwl_methods = cfg["output"].value("compare_hpwl_methods", compare_hpwl_methods);
    MAX_THREADS = cfg["params"]["max_threads"];
    input_dir = fs::path(cfg["input"]["benchmark"]);
    results_dir = fs::path(cfg["output"]["results_dir"].get<std::string>());
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
    // die-relative macro threshold, same 0.02% rule the Visualizer uses to color macros red
    double macro_area_thresh = 0.0002 * db.getDieArea().getArea();
    for (auto& item : db.getComponents())
        if (item.second->getStatus() != FIXED) {
            movable_count++;
            movable_height_sum += item.second->getYsize();
            float node_area = (float)item.second->getXsize() * item.second->getYsize();
            if ((double)node_area > macro_area_thresh) {
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
    if (bins_auto) bins_per_row = formula_bins_per_row;
    Logger::log_info("Grid (ePlace formula): " + std::to_string(formula_bins_per_row)
        + "  [sqrt|B|=" + std::to_string(bins) + ", num_rows=" + std::to_string(num_rows)
        + ", row_cap=" + std::to_string(row_cap) + "]  effective bins_per_row="
        + std::to_string(bins_per_row));
}

/**
 * @brief Smart default: the preconditioner is essential for movable-macro (MMS) convergence but a
 *        wash on fixed-macro designs. When the config did not name enable_preconditioning, turn it
 *        ON iff this design has movable macros. An explicit config value always wins.
 */
void Placer::configurePreconditioner()
{
    if (auto_enable_preconditioning && !precond_explicitly_set) {
        enable_preconditioning = (num_movable_macros > 0);
        Logger::log_info("Preconditioner auto-" + std::string(enable_preconditioning ? "ON" : "OFF")
            + " (" + std::to_string(num_movable_macros) + " movable macros detected)");
    } else {
        Logger::log_info("Movable macros detected: " + std::to_string(num_movable_macros)
            + " (preconditioner " + std::string(enable_preconditioning ? "ON" : "OFF") + ", explicit)");
    }
}

/// @brief Initialize the visualization focus set and canvas — a no-op build without CREATE_VISUALIZATION.
void Placer::initializeVisualization()
{
    #ifdef CREATE_VISUALIZATION
        if(cfg["output"]["visualize"]) {
            initializeFocus();
            viz.init(db.getDieArea());
        }
    #endif
}

/**
 * @brief Reset all nodes and nets in preparation for the next iteration
 */
void Placer::iterationReset()
{
    grid.iterationReset();
    db.iterationReset();
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
    int seed = cfg["params"].value("random_seed", -1);
    std::srand(seed >= 0 ? (unsigned)seed : (unsigned)std::time(nullptr));
    std::mt19937 gauss_gen(seed >= 0 ? (unsigned)seed : (unsigned)std::time(nullptr));
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    float gauss_sigma_x = grid.getDieWidth() * 0.001f;
    float gauss_sigma_y = grid.getDieHeight() * 0.001f;

    Table top;
    top.add_row(RowStream{} << "Initial Placement");
    Table data;
    data.add_row(RowStream{} << "Center" << target_pos.x << target_pos.y);
    data.add_row(RowStream{} << "Sigma" << gauss_sigma_x << gauss_sigma_y);
    top.add_row({data});
    top.format().font_align(FontAlign::center);
    Logger::log_info(top);

    float bin_area_16th = grid.getBinWidth() * grid.getBinHeight() / 16;
    int placed_count = 0, randomized_count = 0;

    for (auto item : db.getComponents()) {
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

        comp_p->checkIfLarge(bin_area_16th);
    }
    Logger::log_info("Initial placement: " + std::to_string(placed_count) +
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
    for (auto item : db.getIOPads())
        item.second->initializeState(item.second->next.node_pos);


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
    for(auto item : db.getComponents()) {
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

    float initial_multiplier = cfg["params"]["density_weight_init_multiplier"];
    density_weight = (HPWL_L1_norm / (density_L1_norm + 1e-8f)) * initial_multiplier;
    last_gwl_L1  = HPWL_L1_norm;    // seeds density_force_fraction for iteration 1
    last_gden_L1 = density_L1_norm;

    Logger::log_info("Initial HPWL gradient L1 norm: " + std::to_string(HPWL_L1_norm));
    Logger::log_info("Initial density gradient L1 norm: " + std::to_string(density_L1_norm));
    Logger::log_info("Initialized density_weight: " + std::to_string(density_weight));
}



/// @brief Log per-iteration step diagnostics (gradient norms, step length, overflow) — DEBUG key only.
void Placer::logStepDiagnostics()
{
    Logger::log_info("=== Step Diagnostics (iteration " + std::to_string(iteration) + ") ===");
    Logger::log_info("  step_length (α̂):  " + PREC_P(step_length, 6));
    Logger::log_info("  momentum_coeff:    " + PREC_P(momentum_coeff, 6));
    Logger::log_info("  nesterov_ak:       " + PREC_P(nesterov_ak, 6));
    Logger::log_info("  backtrack_steps:   " + std::to_string(backtrack_steps));

    // Gradient statistics (from current state used for stepping)
    float grad_L1 = 0.0f, max_grad = 0.0f;
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* n = item.second;
        grad_L1 += fabs(n->current.probe_grad.x) + fabs(n->current.probe_grad.y);
        max_grad = std::max(max_grad, std::max(fabs(n->current.probe_grad.x), fabs(n->current.probe_grad.y)));
    }
    Logger::log_info("  grad L1 norm:      " + SCI(grad_L1));
    Logger::log_info("  max |grad|:        " + SCI(max_grad));
    Logger::log_info("  α̂ * max|grad|:     " + SCI(step_length * max_grad) + "  (max single-step displacement)");

    // Position delta statistics
    float max_node_delta = 0.0f, max_probe_overshoot = 0.0f;
    int clamped_count = 0;
    float die_w = (float)grid.getDieWidth(), die_h = (float)grid.getDieHeight();
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* n = item.second;
        float dx = fabs(n->next.node_pos.x - n->current.node_pos.x);
        float dy = fabs(n->next.node_pos.y - n->current.node_pos.y);
        max_node_delta = std::max(max_node_delta, std::max(dx, dy));

        float ox = fabs(n->next.probe_pos.x - n->next.node_pos.x);
        float oy = fabs(n->next.probe_pos.y - n->next.node_pos.y);
        max_probe_overshoot = std::max(max_probe_overshoot, std::max(ox, oy));

        bool at_boundary = (n->next.node_pos.x <= 0 || n->next.node_pos.x >= die_w ||
                           n->next.node_pos.y <= 0 || n->next.node_pos.y >= die_h);
        if (at_boundary)
            clamped_count++;
    }
    Logger::log_info("  max |Δnode_pos|:   " + SCI(max_node_delta));
    Logger::log_info("  max probe overshoot: " + SCI(max_probe_overshoot));
    Logger::log_info("  nodes at boundary: " + std::to_string(clamped_count));

    // Sample trace: first 3 components
    int count = 0;
    for (auto item : db.getComponents()) {
        if (count >= 3) break;
        if (item.second->getStatus() == FIXED) continue;
        Node* n = item.second;
        Logger::log_info("  --- Sample node: " + n->getName() + " ---");
        Logger::log_info("    current.node_pos (u_k):   (" + PREC_P(n->current.node_pos.x, 2) + ", " + PREC_P(n->current.node_pos.y, 2) + ")");
        Logger::log_info("    current.probe_pos (v_k):  (" + PREC_P(n->current.probe_pos.x, 2) + ", " + PREC_P(n->current.probe_pos.y, 2) + ")");
        Logger::log_info("    current.probe_grad:       (" + SCI(n->current.probe_grad.x) + ", " + SCI(n->current.probe_grad.y) + ")");
        Logger::log_info("    α̂ * grad:                 (" + SCI(step_length * n->current.probe_grad.x) + ", " + SCI(step_length * n->current.probe_grad.y) + ")");
        Logger::log_info("    next.node_pos (u_{k+1}):  (" + PREC_P(n->next.node_pos.x, 2) + ", " + PREC_P(n->next.node_pos.y, 2) + ")");
        Logger::log_info("    next.probe_pos (v_{k+1}): (" + PREC_P(n->next.probe_pos.x, 2) + ", " + PREC_P(n->next.probe_pos.y, 2) + ")");
        Logger::log_info("    next.probe_grad:          (" + SCI(n->next.probe_grad.x) + ", " + SCI(n->next.probe_grad.y) + ")");
        count++;
    }
    Logger::log_info("=== End Step Diagnostics ===");
}

/// @brief Save current movable + filler positions as the best-so-far solution (divergence guard).
void Placer::snapshotBestPlacement()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->best_solution_pos = item.second->next.node_pos;
    }
    for (auto filler_p : db.getFillers())
        filler_p->best_solution_pos = filler_p->next.node_pos;
}

/// @brief Restore movable + filler positions from the saved best-so-far solution.
void Placer::restoreBestPlacement()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->next.node_pos = item.second->best_solution_pos;
    }
    for (auto filler_p : db.getFillers())
        filler_p->next.node_pos = filler_p->best_solution_pos;
}

AIEPLACE_NAMESPACE_END