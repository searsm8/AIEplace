#include "DCT.h"
#include "AIEplace.h"
#include "JsonUtils.h"
#include <cmath>
#include <cassert>
#include <numeric>

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief Run the ePlace algorithm
 *
 * Performs iterations until the convergence condition is met.
 * Each iteration consists of updating hyperparameters, calling performIteration(),
 * and checking convergence.
 */
void Placer::run()
{
    TIME_FUNCTION();

    // Set the center point of die area as initial placement target
    Position target = Position(grid.getDieWidth()/2, grid.getDieHeight()/2);

    // read init_spread parameter
    float init_spread = cfg["params"].value("init_spread", 0.25f);
    int max_dist = (int)(std::min(grid.getDieWidth(), grid.getDieHeight()) * init_spread);

    initializePlacement(target, 0, max_dist);

    bool converged = false;
    while( !converged )
    {
        performIteration();
        converged = checkConvergence();
    }

}

void Placer::performIteration()
{
    Logger::log_detail("BEGIN iteration " + std::to_string(iteration));
    ++iteration;

    // Iteration 1: compute first gradients and initialize solver state.
    // Probe positions (v_1 = u_1) are already set by initializePlacement().
    if (iteration == 1)
    {
        iterationReset();

        computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
        if (compare_hpwl_methods) compareHpwlPartials();
        computeElectricFields();    // ∇D from ρ → bin eFields

        initializeDensityWeight();
        recordInitialHPWL();
    }

    // Update per-node preconditioner weights (fixed for this iteration, unaffected by backtracking)
    updatePrecondWeights();

    // Algorithm 1 + Algorithm 2: step with backtracking line search.
    // On exit: positions at u_{k+1}, v_{k+1}; gradients at v_{k+1} already computed;
    // nesterov_ak advanced.
    performNextStep(enable_backtracking);

    recordIterationResults();
    printIterationResults();

    if (gamma_schedule)
        updateGamma(ovfw_history.back());
    updateDensityWeight();
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
Placer::Placer(std::string config_filepath) 
        { 
            m_config_filepath = config_filepath;
            // Read configuration file (supports JSON with comments)
            std::ifstream config_file(config_filepath);
            // check if config file was found
            if (!config_file.is_open()) {
                Logger::log_error("Unable to open configuration file: " + config_filepath);
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
            quiet = cfg["output"].value("quiet", false);
            Logger::setup_logging(quiet);
            printWelcomeBanner();
            Logger::log_info("Reading runtime configuration from: " + config_filepath);

            // Read hyperparameters
            base_gamma    = cfg["params"]["init_gamma"];
            gamma_schedule = cfg["params"].value("gamma_schedule", false);
            gamma     = gamma_schedule ? 10.0f * base_gamma : base_gamma;
            inv_gamma = 1.0f / gamma;
            step_length = cfg["params"]["init_step_length"];
            density_weight = 1.0f; // will be updated on iteration 1 after computing gradients

            // Read compute methods
            partials_method = cfg["params"]["partials_compute_method"];
            density_method = cfg["params"]["density_compute_method"];
            Logger::log_info("Partials compute method: " + partials_method);
            Logger::log_info("Density compute method:  " + density_method);

            if (partials_method == "simple")
                initHpwlLut();

            // Read Convergence criteria
            max_iterations = cfg["params"]["convergence_max_iterations"];
            min_iterations = cfg["params"]["convergence_min_iterations"];
            hpwl_improvement_threshold = cfg["params"]["convergence_hpwl_improvement_threshold"];
            overflow_threshold = cfg["params"]["convergence_overflow_threshold"];
            target_density = cfg["params"].value("maximum_utilization", 0.9f);
            enable_backtracking = cfg["params"]["enable_backtracking"];
            enable_momentum = cfg["params"]["enable_momentum"];
            enable_preconditioning = cfg["params"].value("enable_preconditioning", true);
            enable_density_clamp = cfg["params"].value("enable_density_clamp", true);
            dct_normalize = cfg["params"].value("dct_normalize", true);
            convergence_window = cfg["params"]["convergence_window"];
            convergence_iterations = cfg["params"].value("convergence_iterations", 30);
            max_backtracking_attempts = cfg["params"]["backtrack_max_tries"];
            backtrack_epsilon = cfg["params"]["backtrack_epsilon"];

            // Read other stuff
            compare_hpwl_methods = cfg["output"].value("compare_hpwl_methods", false);
            MAX_THREADS = cfg["params"]["max_threads"];
            input_dir = fs::path(cfg["input"]["benchmark"]);
            results_dir = fs::path(cfg["output"]["results_dir"].get<std::string>());

            // Grid resolution (default to compile-time BINS_PER_ROW if not specified)
            if (cfg["params"].contains("bins_per_row")) {
                bins_per_row = cfg["params"]["bins_per_row"];
            } else {
                bins_per_row = BINS_PER_ROW;
            }
            if (density_method == "aie" && bins_per_row != BINS_PER_ROW) {
                Logger::log_error("bins_per_row=" + std::to_string(bins_per_row)
                    + " but density_method='aie' requires bins_per_row=" + std::to_string(BINS_PER_ROW)
                    + " (hardware constraint)");
                exit(1);
            }
            Logger::log_info("Grid resolution: " + std::to_string(bins_per_row) + " x " + std::to_string(bins_per_row));

// AI Summary:
// The following section initializes the Xilinx Runtime (XRT) and AI Engine (AIE) drivers if hardware acceleration
// is requested in the config file. It checks the specified compute methods for partials and density, 
// and if either is set to "aie", it attempts to initialize the XRT device and load the xclbin file 
// containing the AIE graph. If XRT support is not compiled in but AIE acceleration is requested, 
// it logs an error and exits.
#ifdef USE_XILINX_XRT
            string xclbin_file = cfg["input"]["xclbin"];
            if(partials_method == "aie" || density_method == "aie") {
                TIME_BLOCK("AIE setup");
                // Open Xilinx Device
                xrt::device device = xrt::device(DEVICE_ID);
                Logger::log_info("Device found -- ID: " + std::to_string(DEVICE_ID));

                // Load xclbin which includes PL and AIE graph
                Logger::log_info("Loading xclbin: \"" + xclbin_file + "\"");
                xrt::uuid xclbin_uuid = device.load_xclbin(xclbin_file);
                Logger::log_info("Success!");

                if(partials_method == "aie") {
                    // Create drivers which handle buffer IO
                    for(int i = 0; i < PARTIALS_GRAPH_COUNT; i++)
                        partials_drivers[i].init(device, xclbin_uuid, i);
                }

                if(density_method == "aie") {
                    density_driver[0].init(device, xclbin_uuid, 0, BINS_PER_ROW); // DCT graph
                    density_driver[1].init(device, xclbin_uuid, 1, BINS_PER_ROW); // IDCT graph
                    density_driver[2].init(device, xclbin_uuid, 2, BINS_PER_ROW); // IDXST graph
                }
            }
#else
            if(partials_method == "aie" || density_method == "aie") {
                Logger::log_error("AIE acceleration requested but not compiled with XRT support!");
                Logger::log_error("Recompile with XILINX_XRT environment variable set, or use CPU methods.");
                exit(1);
            }
#endif

            // Initialize database by reading LEF and DEF design files
            db = DataBase(input_dir); // TODO: Database initialization should be multithreaded?

            // Benchmark-specified maximum_utilization overrides config default
            if (db.getMaximumUtilization() > 0.0f) {
                target_density = db.getMaximumUtilization();
                Logger::log_info("Using benchmark maximum_utilization: " +
                                std::to_string(target_density));
            }

            if(cfg["params"]["enable_filler"])
                db.addFillers(target_density);

            // Preconditioner area normalization: average cell area so area term is O(1) for standard cells
            // (XPlace achieves this by normalizing all coordinates by site_width)
            // Preconditioner normalization: use movable area and count only
            {
                int movable_count = 0;
                for (auto& item : db.getComponents())
                    if (item.second->getStatus() != FIXED) movable_count++;
                avg_node_size = db.getTotalMovableArea() / std::max(1, movable_count);
            }

            db_IO_time = getInterval(pgrm_start_time, getTime());
            Logger::log_info("db read time: " + std::to_string(db_IO_time));

            // Create organized output directory with timestamp and method names
            // Must be after database initialization to get benchmark name
            createRunOutputStructure();

            grid = Grid(db.getDieArea(), bins_per_row, bins_per_row);
            grid.setClampDensity(enable_density_clamp);

            die_size = min( grid.getDieWidth(), grid.getDieHeight() );

            #ifdef CREATE_VISUALIZATION
                initializeFocus();
                if(cfg["output"]["visualize"])
                    viz.init(db.getDieArea());
            #endif
        }

        
/**
 * @brief Estimate ᾱ_k from current positions vs stored prev-iteration data.
 *
 * Reads: node positions, prev_lookahead_pos (v_{k-1}), HPWL partials ∇f_pre(v_k),
 *        prev_lookahead_grad (∇f_pre(v_{k-1})).
 * Does NOT modify any state — call updateBBState() after the committed nudge.
 *
 * @return Clamped BB step estimate [0.0001, 4000].
 */
float Placer::computeLipshitzEstimate()
{
    float pos_norm_sq  = 0.0f;
    float grad_norm_sq = 0.0f;

    auto accumulate = [&](Node* node) {
        // ||v̂_{k+1} - v_k||²
        float dx = node->next.probe_pos.x - node->current.probe_pos.x;
        float dy = node->next.probe_pos.y - node->current.probe_pos.y;
        pos_norm_sq += dx*dx + dy*dy;

        // ||∇f(v̂_{k+1}) - ∇f(v_k)||²
        float dgx = node->next.probe_grad.x - node->current.probe_grad.x;
        float dgy = node->next.probe_grad.y - node->current.probe_grad.y;
        grad_norm_sq += dgx*dgx + dgy*dgy;
    };

    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        accumulate(item.second);
    }
    for (auto filler : db.getFillers())
        accumulate(filler);

    float estimate = sqrtf(pos_norm_sq) / sqrtf(grad_norm_sq + 1e-8f);
    Logger::log_detail("New steplength estimate: " + PREC_P(estimate, 4));
    return std::clamp(estimate, 0.0001f, 4000.0f);
}


/**
 * @brief Update density_weight to increase density force over time.
 * Numerically analyze the current health of the optimization:
 * Is HPWL improving?
 * If yes, continue increasing density_weight slowly.
 * If not, slow or decrease density_weight to allow HPWL forces to have more influence and escape local minima.
 */
void Placer::updateDensityWeight()
{
    if (hpwl_history.size() < 2) return; // need a previous HPWL to measure the trend

    // XPlace step_density_weight (param_scheduler.py): update λ EVERY iteration, slowing to
    // every-3rd only in the early phase or while the forces are mid-balance
    // (density_force_fraction ∈ (0.5, 0.95)). markv1's previous every-3rd-iteration cadence
    // ramped λ ~3× too slowly, so the placement collapsed under wirelength for ~250 iterations
    // before density had any effect.
    bool slow_phase = (iteration < 50) ||
                      (density_force_fraction > 0.5f && density_force_fraction < 0.95f);
    if (slow_phase && (iteration % 3 != 0))
        return;

    float current_hpwl = hpwl_history.back();
    float prev_hpwl    = hpwl_history[hpwl_history.size() - 2];
    float delta_hpwl   = current_hpwl - prev_hpwl;

    float dw_min_step = cfg["params"]["density_weight_min_step"]; // μ lower clamp (0.95)
    float dw_max_step = cfg["params"]["density_weight_max_step"]; // μ growth base (1.05)

    // μ > 1 grows λ. Grow near the max rate (decaying toward 0.98·max) while wirelength is
    // still improving; damp toward ~1.0 once wirelength worsens, so density does not
    // overshoot and blow HPWL up (the late-run divergence observed before this change).
    float mu;
    if (delta_hpwl < 0.0f) {
        mu = dw_max_step * std::max(std::pow(0.9999f, (float)iteration), 0.98f);
    } else {
        float rel_worsening = delta_hpwl / (prev_hpwl + 1e-8f);
        mu = dw_max_step * std::clamp(std::pow(dw_max_step, -rel_worsening * 100.0f),
                                      dw_min_step, dw_max_step);
    }
    density_weight *= mu;

    // Emergency 2x jolt: if overflow has plateaued at a high value, double density_weight
    // to break out of the stall. Modeled after XPlace's enlarge_density mechanism.
    // (param_scheduler.py lines 293-304)
    int plateau_window = cfg["params"]["adaptation_window"];
    float plateau_threshold = cfg["params"]["slow_improvement_threshold"];
    float high_ovfw = cfg["params"]["high_overflow_threshold"];
    int min_jolt_interval = 1000; // Xplace uses 1000 (effectively once per run)

    if (iteration > plateau_window &&
        iteration - last_density_jolt_iter >= min_jolt_interval &&
        ovfw_history.back() > high_ovfw &&
        checkOverflowPlateau(plateau_window, plateau_threshold))
    {
        density_weight *= 2.0f;
        last_density_jolt_iter = iteration;
        Logger::log_info("Overflow plateau detected (ovfw=" +
            PREC(ovfw_history.back()) + "), 2x density weight jolt -> " +
            PREC(density_weight));
    }

    // Escalate preconditioner: double precond_coef every 20 iterations once overflow < 0.3
    // This progressively tightens macro movement in late placement (XPlace param_scheduler.py:340-347)
    if (enable_preconditioning && ovfw_history.back() < 0.3f && precond_coef < 1024.0f) {
        if (iteration % 20 == 0) {
            precond_coef *= 2.0f;
            Logger::log_detail("Preconditioner escalation: precond_coef=" + PREC(precond_coef));
        }
    }
}


/**
 * @brief Check if overflow has plateaued over a recent window.
 *
 * Returns true if the relative range (max - min) / mean of the last
 * `window` overflow values is below `threshold`. Matches XPlace's
 * check_plateau() in param_scheduler.py.
 */
bool Placer::checkOverflowPlateau(int window, float threshold)
{
    if ((int)ovfw_history.size() < window) return false;
    auto begin = ovfw_history.end() - window;
    auto end = ovfw_history.end();
    float min_val = *std::min_element(begin, end);
    float max_val = *std::max_element(begin, end);
    float mean_val = std::accumulate(begin, end, 0.0f) / window;
    return (max_val - min_val) / (mean_val + 1e-8f) < threshold;
}


/**
 * @brief Detect divergence, mirroring XPlace check_divergence (param_scheduler.py).
 *
 * Fires only once the recent HPWL mean has climbed meaningfully above the best known
 * HPWL (so the healthy early phase, where HPWL rises as cells spread, is not flagged)
 * AND overflow is no longer making progress — it has grown past its best, plateaued
 * high, or is fluctuating upward. Reference is the primary best (converged) if we have
 * one, else the lowest-overflow fallback so the guard is useful even before convergence.
 */
bool Placer::checkDivergence(int window, float threshold)
{
    // Reference the CONVERGED best only (XPlace check_divergence returns False while
    // best_metric["hpwl"] is inf). best_fallback tracks the newest lowest-overflow point,
    // so on a healthy monotonic descent a trailing-mean-vs-newest comparison always reads
    // "worse than best" on both HPWL and overflow and false-fires the guard — that killed
    // adaptec2 at iter 332 with overflow still dropping ~2%/iter toward the 0.07 threshold.
    if (!best_primary.valid) return false;
    const BestSolution& best = best_primary;
    if ((int)hpwl_history.size() <= window) return false;

    auto hpwl_begin = hpwl_history.end() - window;
    float wl_mean = std::accumulate(hpwl_begin, hpwl_history.end(), 0.0f) / window;
    float wl_ratio = (wl_mean - best.hpwl) / (best.hpwl + 1e-8f);
    if (wl_ratio <= threshold * 1.2f)
        return false;  // HPWL still near its best → not diverging

    // HPWL is rising above best; classify by how overflow behaves over the window.
    auto ovfw_begin = ovfw_history.end() - window;
    float ovfw_mean = std::accumulate(ovfw_begin, ovfw_history.end(), 0.0f) / window;
    float ovfw_min  = *std::min_element(ovfw_begin, ovfw_history.end());
    float ovfw_max  = *std::max_element(ovfw_begin, ovfw_history.end());
    int rises = 0;
    for (auto it = ovfw_begin + 1; it != ovfw_history.end(); ++it)
        if (*it > *(it - 1)) rises++;
    float ovfw_up_frac = (float)rises / (window - 1);

    float ovfw_ratio = (ovfw_mean - std::max(overflow_threshold, best.overflow)) /
                       (best.overflow + 1e-8f);

    if (ovfw_ratio > threshold)                                  return true; // overflow grew past best
    if ((ovfw_max - ovfw_min) / (ovfw_mean + 1e-8f) < threshold) return true; // plateaued high
    if (ovfw_up_frac > 0.6f)                                     return true; // fluctuating upward
    return false;
}


/**
 * @brief Update per-node preconditioner weights (diagonal preconditioner).
 *
 * Each node's gradient is divided by its precond_weight before the Nesterov step.
 * weight = max(1.0, num_pins + precond_coef * density_weight * area)
 *
 * Large macros (many pins, large area) get heavy damping, while standard cells
 * are barely affected. The precond_coef escalates over time (see updateDensityWeight),
 * progressively tightening macro movement as placement matures.
 *
 * Reference: XPlace param_scheduler.py:349-364, calculator.py:5-8
 */
void Placer::updatePrecondWeights()
{
    float lambda_area_coef = precond_coef * density_weight;

    // Accumulate the two force-mass components for density_force_fraction:
    //   a1 = wirelength mass (pin count per node), a2 = density mass (λ · normalized area).
    // density_force_fraction = ‖a2‖₁ / (‖a1‖₁ + ‖a2‖₁) ∈ [0,1] measures how balanced the
    // wirelength and density forces are — the scale-invariant progress signal that drives
    // the density-weight schedule (XPlace param_scheduler.update_precond_weight, "weighted_weight").
    // Computed even when preconditioning is disabled, since the schedule still consumes it.
    float a1_norm = 0.0f, a2_norm = 0.0f;

    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* node = item.second;
        float num_pins = (float)node->getNets().size();
        float norm_area = node->getArea() / avg_node_size;
        float a2 = lambda_area_coef * norm_area;
        a1_norm += num_pins;
        a2_norm += a2;
        if (enable_preconditioning)
            node->precond_weight = std::max(1.0f, num_pins + a2);
    }
    for (auto filler : db.getFillers()) {
        float norm_area = filler->getArea() / avg_node_size;
        float a2 = lambda_area_coef * norm_area;
        a2_norm += a2;  // fillers carry no pins, so they add no wirelength mass
        if (enable_preconditioning)
            filler->precond_weight = std::max(1.0f, a2);
    }

    density_force_fraction = a2_norm / (a1_norm + a2_norm + 1e-8f);
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
 * @brief Initialize placement of all moveable nodes randomly, clustered about the target position
 *
 * @param target_pos Position around which nodes are spread
 * @param min_dist Minimum distance from target_pos a node can appear
 * @param max_dist Maximum distance from target_pos a node can appear
 */
void Placer::initializePlacement(Position target_pos, int min_dist, int max_dist)
{
    Logger::log_trace("Begin initializePlacement()");
    // Fixed seed (random_seed >= 0) gives identical initial placement across runs — needed for
    // controlled A/B tests; default -1 keeps the time-based seed.
    int seed = cfg["params"].value("random_seed", -1);
    std::srand(seed >= 0 ? (unsigned)seed : (unsigned)std::time(nullptr));

    Table top;
    top.add_row(RowStream{} << "Initial Placement");
    Table data;
    data.add_row(RowStream{} << "Center" << target_pos.x << target_pos.y);
    data.add_row(RowStream{} << "Min dist" << min_dist);
    data.add_row(RowStream{} << "Max dist" << max_dist); 
    top.add_row({data});
    top.format().font_align(FontAlign::center);
    Logger::log_info(top);

    float bin_area_16th = grid.getBinWidth() * grid.getBinHeight() / 16;
    int placed_count = 0, randomized_count = 0;

    for (auto item : db.getComponents()) {
        Component* comp = item.second;

        // If the benchmark provides an initial placement (PLACED status from DEF),
        // use it — this gives a much better starting point than random center placement.
        // Only randomize truly UNPLACED components.
        if (comp->getStatus() == PLACED || comp->getStatus() == FIXED) {
            comp->initializeState(comp->next.node_pos); // use position from DEF parser
            placed_count++;
        } else {
            int range = std::max(1, max_dist - min_dist);
            int x_offset = min_dist + rand() % range;
            if(rand()%2 == 1) x_offset *= -1;
            int y_offset = min_dist + rand() % range;
            if(rand()%2 == 1) y_offset *= -1;
            Position init_pos = target_pos + Position(x_offset, y_offset);
            comp->initializeState(init_pos);
            randomized_count++;
        }

        comp->checkIfLarge(bin_area_16th);
    }
    Logger::log_info("Initial placement: " + std::to_string(placed_count) +
                     " from benchmark, " + std::to_string(randomized_count) + " randomized");

    // Place Fillers uniformly at random across the whole die (XPlace get_filler_pos):
    // fillers must represent whitespace everywhere, so unlike the real cells they are NOT
    // clustered at the center — spreading them seeds the density model with the vacant
    // regions that the real cells should eventually flow into.
    for (auto filler : db.getFillers()) {
        Position init_pos(rand() % grid.getDieWidth(), rand() % grid.getDieHeight());
        filler->initializeState(init_pos);
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
    for (auto filler : db.getFillers()) {
        Gradient electro_force = computeElectrostaticForce(filler);
        density_L1_norm += fabs(electro_force.x) + fabs(electro_force.y);
    }

    float initial_multiplier = cfg["params"]["density_weight_init_multiplier"];
    density_weight = (HPWL_L1_norm / (density_L1_norm + 1e-8f)) * initial_multiplier;

    Logger::log_info("Initial HPWL gradient L1 norm: " + std::to_string(HPWL_L1_norm));
    Logger::log_info("Initial density gradient L1 norm: " + std::to_string(density_L1_norm));
    Logger::log_info("Initialized density_weight: " + std::to_string(density_weight));
}



/**
 * @brief Check if the placement algorithm has converged
 *
 * Convergence is determined by two criteria that must both be met:
 * 1. Overflow is below the threshold (relative to total cell area)
 * 2. HPWL improvement over a window of iterations is below the threshold
 *
 * Also enforces minimum iteration count and uses maximum iterations as a fallback.
 *
 * @return true if converged (or max iterations reached), false otherwise
 */
bool Placer::checkConvergence()
{
    // Safety fallback: max iterations
    if (iteration >= max_iterations) {
        Logger::log_info("Stopping: reached maximum iterations (" +
                        std::to_string(max_iterations) + ")");
        return true;
    }

    // Enforce minimum iterations before any checks
    if (iteration < min_iterations)
        return false;

    float overflow = ovfw_history.back();
    float current_hpwl = hpwl_history.back();

    // NaN detection — stop immediately
    if (std::isnan(overflow) || std::isnan(current_hpwl)) {
        Logger::log_info("Stopping: NaN detected at iteration " +
                        std::to_string(iteration));
        return true;
    }

    // Divergence guard (XPlace need_to_early_stop / check_divergence).
    const BestSolution& best_ref = best_primary.valid ? best_primary
                                 : best_fallback.valid ? best_fallback
                                 : best_primary;

    // Coarse backstop: HPWL has blown past 2x the best known solution.
    if (best_ref.valid && current_hpwl > 2.0f * best_ref.hpwl) {
        Logger::log_info("Stopping: divergence detected at iteration " +
                        std::to_string(iteration) +
                        " (HPWL " + std::to_string(current_hpwl) +
                        " > 2x best " + std::to_string(best_ref.hpwl) +
                        " from iter " + std::to_string(best_ref.iteration) + ")");
        return true;
    }

    // Fine-grained guard, armed only once the run is in the near-converged band
    // (overflow < 5x the stop threshold). During the high-overflow spreading phase
    // HPWL naturally rises and overflow is noisy, which would otherwise be misread as
    // divergence. Each detection burns 6 life; a hard overflow plateau ends it outright.
    if (iteration > 100 && best_ref.valid && overflow < 5.0f * overflow_threshold) {
        if (checkDivergence(3, 0.01f * overflow))
            life -= 6;
        if (overflow >= overflow_threshold && checkOverflowPlateau(50, 0.05f))
            life -= MAX_LIFE;
        if (life <= 0) {
            Logger::log_info("Stopping: divergence guard exhausted at iteration " +
                            std::to_string(iteration) + " (best HPWL " +
                            std::to_string(best_ref.hpwl) + " from iter " +
                            std::to_string(best_ref.iteration) + ")");
            return true;
        }
    }

    // Stagnation detection: DISABLED for now — need to let density weight grow
    // longer before declaring stagnation. Re-enable once density schedule is tuned.
    // static constexpr int STAGNATION_WINDOW = 50;
    // if ((int)hpwl_history.size() > STAGNATION_WINDOW + 1) {
    //     float old_hpwl = hpwl_history[hpwl_history.size() - STAGNATION_WINDOW - 1];
    //     float old_ovfw = ovfw_history[ovfw_history.size() - STAGNATION_WINDOW - 1];
    //     bool hpwl_worsening = current_hpwl > old_hpwl * 1.01f;
    //     bool ovfw_not_improving = overflow > old_ovfw * 0.95f;
    //     if (hpwl_worsening && ovfw_not_improving) {
    //         Logger::log_info("Stopping: stagnation detected at iteration " +
    //                         std::to_string(iteration) +
    //                         " (HPWL worsening: " + std::to_string(old_hpwl) +
    //                         " -> " + std::to_string(current_hpwl) +
    //                         ", overflow stalled: " + std::to_string(old_ovfw) +
    //                         " -> " + std::to_string(overflow) +
    //                         " over last " + std::to_string(STAGNATION_WINDOW) + " iters)");
    //         return true;
    //     }
    // }

    // Overflow countdown — XPlace-inspired convergence mechanism
    // Once overflow drops below threshold, count down convergence_iterations then stop.
    if (overflow < overflow_threshold) {
        if (convergence_iterations_remaining < 0) {
            // First crossing below threshold — start countdown
            convergence_iterations_remaining = convergence_iterations;
            Logger::log_info("Overflow below " + PREC(overflow_threshold) +
                            ", starting convergence countdown (" +
                            std::to_string(convergence_iterations) + " iterations)");
        }
        convergence_iterations_remaining--;

        // Run the full countdown after masked overflow first crosses the threshold, rather
        // than stopping on the first crossing. HPWL is already plateaued by this point, so a
        // HPWL-plateau early-out would stop immediately and leave overflow (and thus the
        // physical spread) worse than it needs to be; the countdown lets overflow keep
        // dropping toward XPlace's converged regime. (Mirrors XPlace's post-threshold life.)
        if (convergence_iterations_remaining <= 0) {
            Logger::log_info("Convergence achieved at iteration " +
                            std::to_string(iteration) +
                            " (overflow countdown complete)");
            return true;
        }

        Logger::log_detail("Convergence countdown: " +
                          std::to_string(convergence_iterations_remaining) + " remaining");
    } else {
        // Overflow rose back above threshold — reset countdown
        if (convergence_iterations_remaining >= 0) {
            Logger::log_detail("Overflow rose above threshold, resetting convergence countdown");
            convergence_iterations_remaining = -1;
        }
    }

    return false;
}


/**
 * @brief Subtract electrostatic force from probe_grad in-place to form total gradient.
 *
 * Called after computeAllPartials() which accumulates HPWL into next.probe_grad.
 * For components: next.probe_grad -= electro (electro already includes λ weighting)
 * For fillers: next.probe_grad = -electro (no HPWL partials)
 */
void Placer::combineGradients()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->next.probe_grad -= computeElectrostaticForce(item.second);
    }
    for (auto filler : db.getFillers()) {
        filler->next.probe_grad -= computeElectrostaticForce(filler);
    }
}


/**
 * @brief Clamp node positions so the entire cell stays within the die area.
 *
 * The node position is the lower-left corner of the cell, so the upper bound
 * must account for the cell's width/height to prevent the right/top edge from
 * extending past the die boundary.
 */
void Placer::enforceDieBoundaries(Node* node_p)
{
    float max_x = (float)grid.getDieWidth()  - node_p->getXsize();
    float max_y = (float)grid.getDieHeight() - node_p->getYsize();

    // Clamp node_pos
    node_p->next.node_pos.x = std::clamp(node_p->next.node_pos.x, 0.0f, max_x);
    node_p->next.node_pos.y = std::clamp(node_p->next.node_pos.y, 0.0f, max_y);

    // Clamp probe_pos
    node_p->next.probe_pos.x = std::clamp(node_p->next.probe_pos.x, 0.0f, max_x);
    node_p->next.probe_pos.y = std::clamp(node_p->next.probe_pos.y, 0.0f, max_y);
}


/**
 * @brief Save current positions and gradients as "previous" for BB estimate and backtracking.
 * Must be called before stepAllNodes() so prev fields serve as the restore point.
 */
void Placer::advanceIterationState()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->cacheState();
    }
    for (auto filler : db.getFillers())  filler->cacheState();
}

/**
 * @brief Perform Nesterov gradient step for all nodes (Algorithm 1, Lines 2 & 4).
 *
 * Delegates to Node::step() which reads from current state and writes to next.
 * The momentum coefficient is computed by the caller (computeNextStep owns Nesterov state).
 *
 * @param mom_coeff Momentum coefficient: (a_k - 1) / a_{k+1}, or 0 if momentum disabled.
 */
void Placer::stepAllNodes()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->step(step_length, momentum_coeff);
        enforceDieBoundaries(item.second);
    }
    for (auto filler : db.getFillers()) {
        filler->step(step_length, momentum_coeff);
        enforceDieBoundaries(filler);
    }
}

/**
 * @brief Algorithm 2 (BkTrk): Backtracking line search for step length.
 *
 * Uses the Barzilai-Borwein (Lipschitz) estimate to validate the step length.
 * The do-while loop takes trial steps with the current step_length, recomputes
 * gradients at the trial position, and checks if α̂ ≤ ε · fresh_bb.
 * If rejected, positions are restored and step_length is updated to the fresh estimate.
 *
 * After the loop, the accepted trial step is committed (positions already at u_{k+1}, v_{k+1})
 * and the Nesterov coefficient is advanced.
 */
void Placer::performNextStep(bool backtracking_enabled)
{
    // Algorithm 1, Line 3: compute momentum coefficient for this iteration
    float a_next = (1.0f + sqrtf(4.0f * nesterov_ak * nesterov_ak + 1.0f)) / 2.0f;
    momentum_coeff = enable_momentum ? (nesterov_ak - 1.0f) / a_next : 0.0f;
    nesterov_ak = a_next;

    // step_length (α̂) carries over from previous iteration (or warmup default)
    advanceIterationState();  // copy next state into current state

    // Algorithm 2: Backtracking
    int tries = 0;
    float prev_step_length;
    do {
        // Lines 2 & 3: trial step using existing step_length from previous iteration
        stepAllNodes();

        iterationReset();
        // Recompute gradients at trial v̂ (the expensive part, accelerated on AIEs)
        computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
        computeElectricFields();    // ∇D from ρ → bin eFields
        combineGradients();         // add electro in-place: next.probe_grad becomes total ∇f

        prev_step_length = step_length;

        // new steplength estimate at probe position: 
        // α = 1 / L = ||v̂ - v_k|| / ||∇f(v̂) - ∇f(v_k)||
        step_length = computeLipshitzEstimate();

        // Accept if α̂ ≤ ε · fresh_bb (step is not too aggressive)
    } while(backtracking_enabled &&
            prev_step_length > backtrack_epsilon * step_length && // epsilon condition
            ++tries < max_backtracking_attempts);

    backtrack_steps = tries; // for logging

    if (Logger::isKeyActive("DEBUG")) logStepDiagnostics();
}

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

        if (n->next.node_pos.x <= 0 || n->next.node_pos.x >= die_w ||
            n->next.node_pos.y <= 0 || n->next.node_pos.y >= die_h)
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

void Placer::snapshotBestPlacement()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->best_solution_pos = item.second->next.node_pos;
    }
    for (auto filler : db.getFillers())
        filler->best_solution_pos = filler->next.node_pos;
}

void Placer::restoreBestPlacement()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->next.node_pos = item.second->best_solution_pos;
    }
    for (auto filler : db.getFillers())
        filler->next.node_pos = filler->best_solution_pos;
}

AIEPLACE_NAMESPACE_END