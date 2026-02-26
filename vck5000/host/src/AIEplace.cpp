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
    //computeAllPartials_CPU();
    //comparePartialResults();

    // Compute Electric Fields in each bin
    computeOverlaps(); // Density Map computation

    // Compute E-fields based on density map
    computeElectricFields();

    //normalizeElectricFields(); // is this needed? is it helpful?

    // DEBUGGING
    //db.printOverlaps();
    //grid.printOverflows();
    //placer.grid.printElectricFields();

    if(iteration == 1)
    {
        recordInitialHPWL();
        initializeLambda();
    }

    // Perform iteration node movement
    nudgeAllNodes();

    recordIterationResults();

    // Dynamically update hyperparameters based on convergence metrics and iteration progress
    updateHyperparameters();

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
    // Set the center point of die area as initial placement target
    Position<position_type> target =
                Position<position_type>(grid.getDieWidth()/2, grid.getDieHeight()/2);

    std::srand(std::time(nullptr)); // use current time as seed for random generator
    #ifdef CREATE_VISUALIZATION
        initializeFocus();
    #endif
    initializePlacement(target, 0, grid.getDieWidth()/4); // even spread around center
    //initializePlacement(target, 0, 500); // Close placement for testing purposes

    bool converged = false;
    while( !converged )
    {
        TIME_BLOCK("Algorithm Block");

        performIteration();

        // Check for convergence using adaptive criteria
        converged = checkConvergence();


        if (converged) {
            if (iteration >= cfg["params"]["max_iterations"]) {
                Logger::log_info("Convergence: Reached maximum iterations (" +
                                std::to_string(cfg["params"]["max_iterations"].get<int>()) + ")");
            } else {
                Logger::log_info("Convergence: Met HPWL improvement and overflow criteria at iteration " +
                                std::to_string(iteration));
            }
        } else {
            iteration++;
        }
    }

    plotHistories();
    algo_time = getInterval(algo_start, getTime());
}

/**
 * @brief Adaptive hyperparameter schedule based on convergence metrics
 *
 * Goal: modify learning_rate (dynamic step size) using Lipshitz constanst and backtracking per ePlace
 *       modify global_lambda based on iteration progress
 * 
 */
void Placer::updateHyperparameters()
{
    // Warm start, for first few iterations use initial learning rate
    if (iteration < cfg["params"]["warmup_iterations"]) {
        Logger::log_detail("Warmup iteration. Skip hyperparam update.");
        return;
    }

    updateLearningRate();

    updateLambda();
}

/**
 * @brief Dynamically update the learning rate based on observed changes 
 * in positions and gradients. 
 * Computes the L2 norm of position changes and gradient changes 
 * across all nodes, and sets learning_rate to their ratio.
 * 
 */
void Placer::updateLearningRate()
{
    float total_position_change = 0;
    float total_gradient_change = 0;

    for(auto item : db.getComponents()) {
        Node* node_p = item.second;
        Position<position_type>& curr_pos = node_p->getPosition();
        Position<position_type>& prev_pos = node_p->getPrevPosition();
        Position<position_type>& prev_grad = node_p->getPrevGrad();

        float pos_change_x = curr_pos.getX() - prev_pos.getX();
        float pos_change_y = curr_pos.getY() - prev_pos.getY();
        total_position_change += (pos_change_x * pos_change_x + pos_change_y * pos_change_y);

        float grad_change_x = node_p->combined_force.x - prev_grad.getX();
        float grad_change_y = node_p->combined_force.y - prev_grad.getY();
        total_gradient_change += (grad_change_x * grad_change_x + grad_change_y * grad_change_y);

        // Update previous position and gradient for next iteration
        prev_pos = curr_pos;
        prev_grad.setX(node_p->combined_force.x);
        prev_grad.setY(node_p->combined_force.y);
    }

    Logger::log_detail("Total position change: " + SCI(total_position_change));
    Logger::log_detail("Total gradient change: " + SCI(total_gradient_change));
    // Update learning rate to 1/L
    learning_rate = sqrt(total_position_change) / sqrt(total_gradient_change + 1e-8f); // avoid div by 0
    Logger::log_detail("Unclamped learning_rate: " + PREC(learning_rate));
    // Clamp to reasonable range
    learning_rate = std::clamp(learning_rate, 0.0001f, 400.0f);

    Logger::log_detail("Updated learning_rate: " + PREC(learning_rate));
}

/** 
 * @brief Update global lambda to increase density force over time
 * Numerically analyze the current health of the optimization:
 * Is HPWL improving?
 * If yes, continue increasing lambda slowly.
 * If not, decrease lambda to allow HPWL forces to have more influence and escape local minima.
 */
void Placer::updateLambda()
{
    if(iteration % 3 != 0) // only update lambda every few iterations since HPWL can be noisy
    {
        Logger::log_detail("Skipping lambda update on iteration " + std::to_string(iteration));
        return;
    }
    float current_hpwl = hpwl_history.back();
    float prev_hpwl = hpwl_history[hpwl_history.size() - 2]; // safe because we have warmup iterations

    float lambda_min_step_size = cfg["params"]["lambda_min_step_size"];
    float lambda_max_step_size = cfg["params"]["lambda_max_step_size"];
    float lambda_multiplier = lambda_max_step_size;

    if (current_hpwl > prev_hpwl) // if HPWL is not improving, 
    {
        float hpwl_percent_change = 100.0f * (current_hpwl - prev_hpwl) / (prev_hpwl + 1e-8f); // avoid div by 0
        // decrease multiplier to allow HPWL forces to have more influence
        lambda_multiplier = std::max(lambda_min_step_size, std::pow(lambda_max_step_size, 1 - hpwl_percent_change)); 
    }

    // perform the update
    global_lambda *= lambda_multiplier;
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
 * @brief Construct a new Placer object and initialize the placement system
 *
 * Reads configuration from JSON file, initializes the database from LEF/DEF files,
 * sets up the grid structure, and initializes XRT/AIE drivers if hardware acceleration
 * is enabled.
 *
 * @param config_filepath Path to the JSON configuration file
 */
Placer::Placer(std::string config_filepath ) 
        { 
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

            //initialize values from JSON
            partials_method = cfg["params"]["partials_compute_method"];
            density_method = cfg["params"]["density_compute_method"];
            Logger::log_info("Partials compute method: " + partials_method);
            Logger::log_info("Density compute method:  " + density_method);

            gamma = cfg["params"]["gamma"];
            inv_gamma = 1.0f / gamma;
            learning_rate = cfg["params"]["init_learning_rate"];
            global_lambda = 1.0f; // will be updated on iteration 1 after computing gradients
            MAX_THREADS = cfg["params"]["max_threads"];
            input_dir = fs::path(cfg["input"]["benchmark"]);

            string xclbin_file = cfg["input"]["xclbin"];
            result_csv = cfg["output"]["result_csv"];

#ifdef USE_XILINX_XRT
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
                db.addFillers(cfg["params"]["target_utilization"]);

            db_IO_time = getInterval(pgrm_start_time, getTime());
            Logger::log_info("db read time: " + std::to_string(db_IO_time));

            // Create organized output directory with timestamp and method names
            // Must be after database initialization to get benchmark name
            std::string output_dir_str, run_id;
            createRunOutputStructure(output_dir_str, run_id);
            output_dir = fs::path(output_dir_str);

            grid = Grid(db.getDieArea(), BINS_PER_ROW, BINS_PER_ROW); 

            die_size = min( grid.getDieWidth(), grid.getDieHeight() );

            #ifdef CREATE_VISUALIZATION
                if(cfg["output"]["visualize"])
                    viz.init(db.getDieArea());
            #endif
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
 * @brief Set initial global lambda based on ratio of total HPWL gradient to density gradient
 * Compute total force produced by the HPWL gradient,
 * and the total force produced by the density gradient. 
 * Set initial lambda as the ratio between these two to make them roughly balanced,
 * multiplied by a small number which makes the density force initially weaker 
 */
void Placer::initializeLambda()
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

    float initial_multiplier = cfg["params"]["lambda_init_multiplier"];
    global_lambda = (HPWL_L1_norm / (density_L1_norm + 1e-8f)) * initial_multiplier;
    Logger::log_info("Initialized global lambda: " + std::to_string(global_lambda));
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

    node_p->combined_force.x = electro_force.x - partials.x; // we subtract the partials to reduce net size!
    node_p->combined_force.y = electro_force.y - partials.y;

    XY move;
    move.x = learning_rate * node_p->combined_force.x; 
    move.y = learning_rate * node_p->combined_force.y;


    // Update the position of this node
    node_p->translate(move.x, move.y);

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
    // Check max iterations as safety fallback
    if (iteration >= cfg["params"]["max_iterations"])
        return true;

    return false; // TEMP: disable convergence checking for testing purposes

    // Need minimum iterations before checking convergence
    int min_iterations = cfg["params"].contains("min_iterations") ?
                        cfg["params"]["min_iterations"].get<int>() : 10;

    if (iteration < min_iterations)
        return false;

    // Get convergence thresholds from config (with defaults)
    float hpwl_improvement_threshold = cfg["params"].contains("hpwl_improvement_threshold") ?
                                      cfg["params"]["hpwl_improvement_threshold"].get<float>() : 0.01f; // 1% improvement
    float overflow_threshold = cfg["params"].contains("overflow_threshold") ?
                              cfg["params"]["overflow_threshold"].get<float>() : 0.05f; // 5% overflow
    int convergence_window = cfg["params"].contains("convergence_window") ?
                            cfg["params"]["convergence_window"].get<int>() : 5; // Check last 5 iterations

    // Check 1: Overflow of each bin must be below overflow threshold 
    bool overflow_converged = true;

    //DEBUGGING
    //grid.printOverflows();

    for (int col = 0; col < grid.getBinsPerRow(); col++)
        for (int row = 0; row < grid.getBinsPerCol(); row++)
        {
            float overflow = grid.getBin(col, row).getOverflowRatio();
            if (overflow > overflow_threshold) {
                overflow_converged = false;
                if(overflow > overflow_threshold * 2) // only log significant overflows to avoid log spam
                    Logger::log_detail("Bin (" + std::to_string(col) + ", " + std::to_string(row) + ") overflow: " + std::to_string(overflow));
            }
        }


    // Check 2: HPWL improvement over last N iterations must be small
    if (hpwl_history.size() < convergence_window + 1)
        return false;

    float old_hpwl = hpwl_history[hpwl_history.size() - convergence_window - 1];
    float current_hpwl = hpwl_history.back();
    float hpwl_improvement = (old_hpwl - current_hpwl) / old_hpwl;

    bool hpwl_converged = (hpwl_improvement < hpwl_improvement_threshold);

    // Combined convergence: both criteria must be met
    if(overflow_converged && hpwl_converged) {
        return true;
        Logger::log_info("Convergence met at iteration " + std::to_string(iteration) +
                        ", HPWL improvement = " + std::to_string(hpwl_improvement));
    }
    else return false;
}

AIEPLACE_NAMESPACE_END