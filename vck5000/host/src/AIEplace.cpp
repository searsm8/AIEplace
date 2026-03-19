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

    initializePlacement(target, 0, grid.getDieWidth()/4); // even spread around center
    //initializePlacement(target, 0, 500); // Close placement for testing purposes

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
        iterationReset(); // is this needed?

        computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
        computeElectricFields();    // ∇D from ρ → bin eFields

        initializeDensityWeight();
        recordInitialHPWL();
    }

    // Algorithm 1 + Algorithm 2: step with backtracking line search.
    // On exit: positions at u_{k+1}, v_{k+1}; gradients at v_{k+1} already computed;
    // nesterov_ak advanced.
    performNextStep(enable_backtracking);

    recordIterationResults();
    printIterationResults();

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
            bool quiet = cfg["output"].value("quiet", false);
            Logger::setup_logging(quiet);
            printWelcomeBanner();
            Logger::log_info("Reading runtime configuration from: " + config_filepath);

            // Read hyperparameters
            gamma = cfg["params"]["gamma"];
            inv_gamma = 1.0f / gamma;
            step_length = cfg["params"]["init_step_length"];
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
            target_density = cfg["params"]["convergence_target_density"];
            enable_backtracking = cfg["params"]["enable_backtracking"];
            enable_momentum = cfg["params"]["enable_momentum"];
            convergence_window = cfg["params"]["convergence_window"];
            max_backtracking_attempts = cfg["params"]["backtrack_max_tries"];
            backtrack_epsilon = cfg["params"]["backtrack_epsilon"];

            // Read other stuff
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

            if(cfg["params"]["enable_filler"])   
                db.addFillers(target_density);

            db_IO_time = getInterval(pgrm_start_time, getTime());
            Logger::log_info("db read time: " + std::to_string(db_IO_time));

            // Create organized output directory with timestamp and method names
            // Must be after database initialization to get benchmark name
            createRunOutputStructure();

            grid = Grid(db.getDieArea(), bins_per_row, bins_per_row);

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

    for (auto item : db.getComponents())
        accumulate(item.second);
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
    if(iteration % 3 != 0) // only update density_weight every few iterations since HPWL can be noisy
    {
        //Logger::log_detail("Skipping density_weight update on iteration " + std::to_string(iteration));
        return;
    }
    float current_hpwl = hpwl_history.back();
    float prev_hpwl = hpwl_history[hpwl_history.size() - 2]; // safe because we have warmup iterations

    float dw_min_step = cfg["params"]["density_weight_min_step"];
    float dw_max_step = cfg["params"]["density_weight_max_step"];
    float dw_multiplier = dw_max_step;

    if (current_hpwl > prev_hpwl) // if HPWL is not improving,
    {
        // decrease multiplier to allow HPWL forces to have more influence
        float hpwl_percent_change = 100.0f * (current_hpwl - prev_hpwl) / (prev_hpwl + 1e-8f); // avoid div by 0
        dw_multiplier = std::max(dw_min_step, std::pow(dw_max_step, 1 - hpwl_percent_change));
    }

    // perform the update
    density_weight *= dw_multiplier;

    // Emergency 2x jolt: if overflow has plateaued at a high value, double density_weight
    // to break out of the stall. Modeled after XPlace's enlarge_density mechanism.
    // (param_scheduler.py lines 293-304)
    int plateau_window = cfg["params"]["adaptation_window"];
    float plateau_threshold = cfg["params"]["slow_improvement_threshold"];
    float high_ovfw = cfg["params"]["high_overflow_threshold"];
    int min_jolt_interval = 1000;

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
    std::srand(std::time(nullptr)); // use current time as seed for random generator

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
    // For each component that isn't fixed
    for (auto item : db.getComponents()) {
        // Choose a random position based on parameters
        // TODO: Different initial position "shapes" could help with performance?
        // e.g. maybe a donut shape would be good.
        int x_offset = min_dist + rand()%(max_dist-min_dist); // clustered around target
        if(rand()%2 == 1) x_offset *= -1; // 50% chance to negate
        int y_offset = min_dist + rand()%(max_dist-min_dist); // clustered around target
        if(rand()%2 == 1) y_offset *= -1; // 50% chance to negate
        //int x_offset = rand()%(grid.getDieWidth()) - grid.getDieWidth()/2; // Even Spread
        //int y_offset = rand()%(grid.getDieWidth()) - grid.getDieWidth()/2; // Even Spread
        Position init_pos = target_pos + Position(x_offset, y_offset);
        item.second->initializeState(init_pos);

        // if this component is bigger than 1/16th of bin area, set member bool
        item.second->checkIfLarge(bin_area_16th);
    }

    // Place Fillers
    for (auto filler : db.getFillers()) {
        int x_offset = min_dist + rand()%(grid.getDieWidth()/2); // clustered around target
        if(rand()%2 == 1) x_offset *= -1; // 50% chance to negate
        int y_offset = min_dist + rand()%(grid.getDieHeight()/2); // clustered around target
        if(rand()%2 == 1) y_offset *= -1; // 50% chance to negate
        Position init_pos = target_pos + Position(x_offset, y_offset);
        filler->initializeState(init_pos);
    }

    // Initialize pin State (pins are fixed, but need valid probe_pos for gradient computation)
    for (auto item : db.getPins())
        item.second->initializeState(item.second->next.node_pos);


    //printIterationResults(); // Prints "iteration 0" starting statistics
    iteration = 0;

    // TODO
    // Wild and Crazy Idea: wouldn't this have the same effect as slowly increasing the bin's lambda?
    // Add additional large "phantom" macros for experimentation
    // Observe what affect they have,
    // They could be made to have a repulsive affect on the real nodes or macros
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
        Node* node_p = item.second;

        Gradient& partials = node_p->next.probe_grad;  // HPWL-only (before combineGradients)
        HPWL_L1_norm += fabs(partials.x) + fabs(partials.y);

        Gradient electro_force = computeElectrostaticForce(node_p);
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
    // Check 0: Max iterations as safety fallback
    if (iteration >= max_iterations) {
        Logger::log_info("Convergence: Reached maximum iteration " +
                        std::to_string(max_iterations));
        return true;
    }

    // Check 1: Enforce minimum iterations
    if (iteration < min_iterations)
        return false;

    // Check 2: Overflow of each bin must be below overflow threshold 
    bool overflow_converged = false;

    float overflow = grid.computeTotalOverflow( 
                    target_density, 
                    db.computeTotalComponentArea());

    if (overflow < overflow_threshold) {
        overflow_converged = true;
        Logger::log_info("OVERFLOW CONVERGED (Less than: " + std::to_string(overflow_threshold) + ")");
    }

    // Check 3: HPWL improvement over last N iterations must be small
    if (hpwl_history.size() < convergence_window + 1)
        return false;

    float old_hpwl = hpwl_history[hpwl_history.size() - convergence_window - 1];
    float current_hpwl = hpwl_history.back();
    float hpwl_improvement = (old_hpwl - current_hpwl) / old_hpwl;

    bool hpwl_converged = (hpwl_improvement < hpwl_improvement_threshold);
                            //&& (hpwl_improvement > 0.0f);

    Logger::log_detail("HPWL improvement from previous "+ std::to_string(convergence_window) +
                    " iterations: " + PREC(100*hpwl_improvement) +"%");
    // Combined convergence: both criteria must be met
    if(overflow_converged && hpwl_converged) {
        Logger::log_info("CONVERGENCE ACHIEVED at iteration " + std::to_string(iteration));

        return true;
    }
    else return false;
}


/**
 * @brief Computes the gradients of all nodes at their probe positions (v_k).
 * This is a key step that must be performed at the start of each iteration to get the forces.
 * Can be computed on CPU or offloaded to AIE depending on configuration. 
 * Results are cached in node-local fields for reuse.
 */

 // TODO: NOT USED, REMOVE?
void Placer::computeAllProbeGradients()
{
    // Should be non-blocking and multithreaded, as partials and eFields are independent
    computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
    computeElectricFields();    // ∇D from ρ → bin eFields

    combineGradients();         // subtract electro in-place: next.probe_grad becomes total ∇f
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
    for (auto item : db.getComponents()) item.second->cacheState();
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
        combineGradients();         // subtract electro in-place: next.probe_grad becomes total ∇f

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
    for (auto item : db.getComponents())
        item.second->best_solution_pos = item.second->next.node_pos;
    for (auto filler : db.getFillers())
        filler->best_solution_pos = filler->next.node_pos;
}

void Placer::restoreBestPlacement()
{
    for (auto item : db.getComponents())
        item.second->next.node_pos = item.second->best_solution_pos;
    for (auto filler : db.getFillers())
        filler->next.node_pos = filler->best_solution_pos;
}

AIEPLACE_NAMESPACE_END