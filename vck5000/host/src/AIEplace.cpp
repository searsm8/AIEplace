#include "DCT.h"
#include "AIEplace.h"
#include "JsonUtils.h"
#ifdef USE_TBB
#include <tbb/tbb.h>
#endif
#include <cmath>
#include <cassert>

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief Perform a single iteration of the ePlace algorithm
 *
 * Computes partials and electric field simulation based on selected methods in config.
 * Then nudges all nodes based on computed forces.
 * This function will be called repeatedly from Placer::run() until convergence is met.
 */
void Placer::performIteration()
{
    Logger::log_detail("BEGIN iteration " + std::to_string(iteration));

    iterationReset();

    // Compute terms for HPWL partials
    computeAllPartials();

    // DEBUGGING: Compare results to ensure correctness

    // Compute Electric Fields in each bin
    computeOverlaps(); // Density Map computation

    // Compute E-fields based on density map
    computeElectricFields();

    //normalizeElectricFields(); // is this needed? is it helpful?

    // DEBUGGING
    //computeAllPartials_CPU();
    //comparePartialResults();
    //db.printOverlaps();
    //grid.printOverflows();
    //placer.grid.printElectricFields();

    if(iteration == 1)
    {
        recordInitialHPWL();
        initializeDensityWeight();
    }

    nudgeAndUpdate();
    updateDensityWeight();

    recordIterationResults();
    printIterationResults();
}

/**
 * @brief Run the ePlace algorithm
 *
 * Performs iterations until the convergence condition is met.
 * Each iteration consists of updating hyperparameters, calling performIteration(),
 * and checking convergence.
 */
void Placer::run()
{
    algo_start = getTime();
    std::srand(std::time(nullptr)); // use current time as seed for random generator
    // Set the center point of die area as initial placement target
    Position<position_type> target =
                Position<position_type>(grid.getDieWidth()/2, grid.getDieHeight()/2);

    initializePlacement(target, 0, grid.getDieWidth()/4); // even spread around center
    //initializePlacement(target, 0, 500); // Close placement for testing purposes

    bool converged = false;
    while( !converged )
    {
        TIME_BLOCK("Algorithm Block");

        performIteration();

        converged = checkConvergence();

        iteration++;
    }

    plotHistories();
    algo_time = getInterval(algo_start, getTime());
}

/**
 * @brief Construct a new Placer object and initialize the placement system
 *
 * Reads configuration from JSON file, initializes the database from LEF/DEF files,
 * sets up the grid structure, and initializes XRT/AIE drivers if hardware acceleration
 * is enabled.
 *
 * @param config_filepath Path to the JSON configuration file
 */
Placer::Placer(std::string config_filepath) 
        { 
            m_config_filepath = config_filepath;
            printWelcomeBanner();
            // Read configuration file (supports JSON with comments)
            Logger::log_info("Reading runtime configuration from: " + config_filepath);
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

            // Read other stuff
            MAX_THREADS = cfg["params"]["max_threads"];
            input_dir = fs::path(cfg["input"]["benchmark"]);
            results_dir = fs::path(cfg["output"]["results_dir"].get<std::string>());

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

            if(cfg["params"]["use_filler"])   
                db.addFillers(target_density);

            db_IO_time = getInterval(pgrm_start_time, getTime());
            Logger::log_info("db read time: " + std::to_string(db_IO_time));

            // Create organized output directory with timestamp and method names
            // Must be after database initialization to get benchmark name
            createRunOutputStructure();

            grid = Grid(db.getDieArea(), BINS_PER_ROW, BINS_PER_ROW); 

            die_size = min( grid.getDieWidth(), grid.getDieHeight() );

            #ifdef CREATE_VISUALIZATION
                initializeFocus();
                if(cfg["output"]["visualize"])
                    viz.init(db.getDieArea());
            #endif
        }

/**
 * @brief Pure BB step. Estimate ᾱ_k from current positions vs stored prev-iteration data.
 *
 * Reads: node positions, prev_lookahead_pos (v_{k-1}), HPWL partials ∇f_pre(v_k),
 *        prev_lookahead_grad (∇f_pre(v_{k-1})).
 * Does NOT modify any state — call updateBBState() after the committed nudge.
 *
 * @return Clamped BB step estimate [0.0001, 4000].
 */
float Placer::computeBBStep()
{
    float total_pos_change  = 0.0f;
    float total_grad_change = 0.0f;

    for (auto item : db.getComponents()) {
        Node* node = item.second;

        float dx = node->getX() - node->getPrevProbePosition().x;
        float dy = node->getY() - node->getPrevProbePosition().y;
        total_pos_change += dx*dx + dy*dy; // ||v_k − v_{k-1}||

        XY p = getNodePartials(node);                          // ∇f_pre(v_k)
        float dgx = p.x - node->getPrevProbeGrad().getX(); // ∇f_pre(v_k) − ∇f_pre(v_{k-1})
        float dgy = p.y - node->getPrevProbeGrad().getY();
        total_grad_change += dgx*dgx + dgy*dgy; // ||∇f_pre(v_k) − ∇f_pre(v_{k-1})||
    }

    float new_step_size = sqrtf(total_pos_change) / sqrtf(total_grad_change + 1e-8f);
    Logger::log_detail("New step (pre-nudge): " + PREC(new_step_size));
    return std::clamp(bb, 0.0001f, 4000.0f);
}

/**
 * @brief Snapshot current node positions and HPWL partials into lookahead fields.
 *
 * Must be called before nudgeAllNodes(). Stores (v_k, ∇f_pre(v_k)) so that
 * updateBBState() can save the pre-nudge state for the next iteration's BB estimate.
 */
void Placer::snapshotPreNudge()
{
    for (auto item : db.getComponents()) {
        Node* node = item.second;
        node->getProbePosition() = node->getPosition();
        XY p = getNodePartials(node);
        node->getProbeGrad().setX(p.x);
        node->getProbeGrad().setY(p.y);
    }
    for (auto filler : db.getFillers())
        filler->getProbePosition() = filler->getPosition();
}

/**
 * @brief Store the pre-nudge snapshot into prev-lookahead fields for next iteration's BB estimate.
 *
 * Must be called after snapshotPreNudge() and after the committed nudge.
 * Reads from getLookaheadPosition/Grad() (set by snapshotPreNudge()) so that
 * computeBBStep() next iteration gets: dx = û_k − v_k, dg = ∇f_pre(û_k) − ∇f_pre(v_k).
 */
void Placer::updateBBState()
{
    for (auto item : db.getComponents()) {
        Node* node = item.second;
        node->getPrevProbePosition() = node->getProbePosition();  // v_k
        node->getPrevProbeGrad()     = node->getProbeGrad();      // ∇f_pre(v_k)
    }
}

/**
 * @brief Algorithm 2 (BkTrk): backtracking BB step refinement, node nudge, and hyperparameter update.
 *
 * During warmup: plain nudge + BB state init, no density weight update.
 * Post-warmup with backtrack disabled: single nudge with BB step + density weight update.
 * Post-warmup with backtrack enabled:
 *   1. Compute initial ᾱ_k (BB estimate, pre-nudge).
 *   2. Trial nudge û_{k+1} = v_k − ᾱ_k ∇f_pre(v_k).
 *   3. Compute fresh BB from the trial step.
 *   4. If ᾱ_k > ε · fresh_BB: replace ᾱ_k with fresh BB, retry from v_k.
 *   5. Repeat until consistent or max_tries reached.
 *   Density field is held fixed throughout the loop.
 */
void Placer::nudgeAndUpdate()
{
    const bool in_warmup = (iteration <= (int)cfg["params"]["warmup_iterations"]);

    // ── Warmup path: plain nudge, initialize BB state ────────────────────
    if (in_warmup) {
        snapshotPreNudge();
        nudgeAllNodes();
        updateBBState();
        return;
    }

    // ── Algorithm 2: BkTrk ───────────────────────────────────────────────

    // Line 1: compute initial ᾱ_k from previous iteration data (pre-nudge)
    step_length = computeBBStep();

    backtrack_steps = 0;
    const bool  enabled   = cfg["params"]["backtrack_enabled"];
    const int   max_tries = cfg["params"]["backtrack_max_tries"];
    const float epsilon   = cfg["params"]["backtrack_epsilon"];

    if (!enabled) {
        snapshotPreNudge();
        nudgeAllNodes();
        updateBBState();
        updateDensityWeight();
        return;
    }

    // Snapshot v_k into node-local lookahead fields
    snapshotPreNudge();

    for (int t = 0; t < max_tries; t++)
    {
        // Lines 2/6: û_{k+1} = v_k − ᾱ_k ∇f_pre(v_k)
        nudgeAllNodes();

        // Compute ∇f_pre(û_{k+1}) at trial positions (density field held fixed)
        computeAllPartials();

        // Compute fresh BB estimate: ‖û − v_k‖ / ‖∇f_pre(û) − ∇f_pre(v_k)‖
        float num_sq = 0.0f, den_sq = 0.0f;
        for (auto item : db.getComponents()) {
            Node* node = item.second;
            float dx = node->getX() - node->getProbePosition().x;
            float dy = node->getY() - node->getProbePosition().y;
            num_sq += dx*dx + dy*dy;

            XY p = getNodePartials(node);
            float dgx = p.x - node->getProbeGrad().getX();
            float dgy = p.y - node->getProbeGrad().getY();
            den_sq += dgx*dgx + dgy*dgy;
        }
        float fresh_bb = sqrtf(num_sq) / sqrtf(den_sq + 1e-8f);

        // Line 4: accept if ᾱ_k ≤ ε · fresh_BB
        if (step_length <= epsilon * fresh_bb)
            break;

        // Condition failed (Lines 5–7): update ᾱ_k and restore to v_k
        backtrack_steps++;
        step_length = std::clamp(fresh_bb, 0.0001f, 400.0f);

        if (t < max_tries - 1) {
            for (auto item : db.getComponents()) {
                Node* node = item.second;
                node->setX(node->getProbePosition().x);
                node->setY(node->getProbePosition().y);
            }
            for (auto filler : db.getFillers()) {
                filler->setX(filler->getProbePosition().x);
                filler->setY(filler->getProbePosition().y);
            }
            // Restore ∇f_pre(v_k) from snapshot (avoids full recomputation)
            for (auto item : db.getComponents()) {
                Node* node = item.second;
                if (partials_method == "aie") {
                    node->partials_aie.x = node->getProbeGrad().getX();
                    node->partials_aie.y = node->getProbeGrad().getY();
                } else {
                    node->terms_cpu.partials.x = node->getProbeGrad().getX();
                    node->terms_cpu.partials.y = node->getProbeGrad().getY();
                }
            }
        }
        // On the last try: accept whatever position we have (ensure progress)
    }
    Logger::log_detail("BkTrk steps taken: " + std::to_string(backtrack_steps));

    // Committed position is set; partials at committed position are in memory
    updateBBState();
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
}

/**
 * @brief Reset all nodes and nets in preparation for the next iteration
 */
void Placer::iterationReset()
{
    grid.iterationReset();
    db.iterationReset();

    all_partials.clear();
    simple_partials.clear();
}


/**
 * @brief Initialize placement of all moveable nodes randomly, clustered about the target position
 *
 * @param target_pos Position around which nodes are spread
 * @param min_dist Minimum distance from target_pos a node can appear
 * @param max_dist Maximum distance from target_pos a node can appear
 */
void Placer::initializePlacement(Position<position_type> target_pos, int min_dist, int max_dist)
{
    Logger::log_trace("Begin initializePlacement()");
    Table top;
    top.add_row(RowStream{} << "Initial Placement");
    Table data;
    data.add_row(RowStream{} << "Center" << target_pos.getX() << target_pos.getY());
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
        Position<position_type> init_pos = target_pos + Position<position_type>(x_offset, y_offset);
        item.second->setPosition(init_pos);

        // if this component is bigger than 1/16th of bin area, set member bool
        item.second->checkIfLarge(bin_area_16th);
    }

    // Place Fillers
    for (auto filler : db.getFillers()) {
        int x_offset = min_dist + rand()%(grid.getDieWidth()/2); // clustered around target
        if(rand()%2 == 1) x_offset *= -1; // 50% chance to negate
        int y_offset = min_dist + rand()%(grid.getDieHeight()/2); // clustered around target
        if(rand()%2 == 1) y_offset *= -1; // 50% chance to negate
        Position<position_type> init_pos = target_pos + Position<position_type>(x_offset, y_offset);
        filler->setPosition(init_pos);
    }


    //printIterationResults(); // Prints "iteration 0" starting statistics
    iteration = 1;

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

        XY partials = getNodePartials(node_p);
        HPWL_L1_norm += fabs(partials.x) + fabs(partials.y);

        XY electro_force = computeElectrostaticForce(node_p);
        density_L1_norm += fabs(electro_force.x) + fabs(electro_force.y);
    }

    float initial_multiplier = cfg["params"]["density_weight_init_multiplier"];
    density_weight = (HPWL_L1_norm / (density_L1_norm + 1e-8f)) * initial_multiplier;
    Logger::log_info("Initialized density_weight: " + std::to_string(density_weight));
}



/***************
 * CPU FUNCTIONS - Always available
 *
 * These functions run on the host CPU and don't require XRT or VCK5000 hardware.
 * Partials functions moved to Partials.cpp
 * Density functions moved to Density.cpp
 ****************/



/**
 * @brief Nudge all movable nodes based on computed forces
 *
 * Iterates through all components in the database and calls nudgeNode()
 * for each one to update their positions.
 */
void Placer::nudgeAllNodes()
{
    //Logger::log_detail("Begin nudgeAllNodes()");
    for (auto item : db.getComponents())
        nudgeNode(item.second);
    
    // Also nudge fillers if they exist
    for (auto filler : db.getFillers())
        nudgeNode(filler);
}

/**
 * @brief Update the position of a single node based on electric field and HPWL forces
 *
 * Computes the electric force from density (bin overlaps) and combines it with
 * HPWL partial derivatives to calculate a move vector. The node position is updated
 * and clamped to die boundaries.
 *
 * @param node_p Pointer to the node to be nudged
 */
void Placer::nudgeNode(Node* node_p)
{
    XY partials = getNodePartials(node_p);
    XY electro_force = computeElectrostaticForce(node_p);

    node_p->combined_force.x = electro_force.x - partials.x; // negate partials to move down the gradient
    node_p->combined_force.y = electro_force.y - partials.y;

    XY move;
    move.x = step_length * node_p->combined_force.x;
    move.y = step_length * node_p->combined_force.y;


    // Update the position of this node
    node_p->translate(move);

    // Enforce die boundaries
    if (node_p->getX() < 0) node_p->setX(0);
    if (node_p->getY() < 0) node_p->setY(0);
    float max_x = db.getDieArea().getXsize();
    float max_y = db.getDieArea().getYsize();
    if (node_p->getX() > max_x) node_p->setX(max_x);
    if (node_p->getY() > max_y) node_p->setY(max_y);

    // DEBUGGING
    //cout << "NudgeNode(): "<< node_p->getName() 
    //    << " grad(" << wirelen_gradient.x << ", " << wirelen_gradient.y << ")"
    //    << "\telectro(" << electro_force.x << ", " << electro_force.y << ")" << endl;
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

    Logger::log_detail("Current overflow: " + std::to_string(overflow));
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

    bool hpwl_converged = (hpwl_improvement < hpwl_improvement_threshold)
                            && (hpwl_improvement > 0.0f);

    // Combined convergence: both criteria must be met
    if(overflow_converged && hpwl_converged) {
        Logger::log_info("CONVERGENCE ACHIEVED at iteration " + std::to_string(iteration));

        Logger::log_info("HPWL improvement from previous "+ std::to_string(convergence_window) +
                        " iterations: " + std::to_string(100*hpwl_improvement) +"%");
        return true;
    }
    else return false;
}

AIEPLACE_NAMESPACE_END