#include "Placer.h"

namespace AIEPLACE_NAMESPACE {

  void ConfigParser::parse(const std::string& config_filepath, HyperParameters& params)
  {
    params.config_filepath = config_filepath;

    // Read hyperparameters
    params.gamma = cfg["params"]["gamma"];
    params.inv_gamma = 1.0f / params.gamma;
    params.step_length = cfg["params"]["init_step_length"];
    params.density_weight = 1.0f; // will be updated on iteration 1 after computing gradients

    // Read compute methods 
    params.partials_method = cfg["params"]["partials_compute_method"];
    params.density_method = cfg["params"]["density_compute_method"];
    Logger::log_info("Partials compute method: " + params.partials_method);
    Logger::log_info("Density compute method:  " + params.density_method);

    // Read Convergence criteria
    params.max_iterations = cfg["params"]["convergence_max_iterations"];
    params.min_iterations = cfg["params"]["convergence_min_iterations"];
    params.hpwl_improvement_threshold = cfg["params"]["convergence_hpwl_improvement_threshold"];
    params.overflow_threshold = cfg["params"]["convergence_overflow_threshold"];
    params.target_density = cfg["params"]["convergence_target_density"];
    params.enable_backtracking = cfg["params"]["enable_backtracking"];
    params.enable_momentum = cfg["params"]["enable_momentum"];
    params.warmup_iterations = cfg["params"]["warmup_iterations"];
    params.convergence_window = cfg["params"]["convergence_window"];
    params.max_backtracking_attempts = cfg["params"]["backtrack_max_tries"];
    params.backtrack_epsilon = cfg["params"]["backtrack_epsilon"];
    params.enable_filler = cfg["params"]["enable_filler"];

    // Read other stuff
    params.MAX_THREADS = cfg["params"]["max_threads"];
    params.input_dir = fs::path(cfg["input"]["benchmark"]);
    params.results_dir = fs::path(cfg["output"]["results_dir"].get<std::string>());

    params.xclbin_file = cfg["input"]["xclbin"];

    params.create_vizualisation = cfg["output"]["visualize"];
  }

  void ConfigParser::readconfig(const std::string config_filepath) {
    // Read configuration file (supports JSON with comments)
    Logger::log_info("Reading runtime configuration from: " + config_filepath);

    // check if config file was found
    std::ifstream config_file(config_filepath);
    if (!config_file.is_open()) {
      Logger::log_error("Unable to open configuration file: " + config_filepath);
      exit(1);
    }

    // Read file content and strip comments
    std::stringstream buffer;
    buffer << config_file.rdbuf();
    config_file.close();
    std::string config_content = buffer.str();
    std::string json_content = JsonUtils::stripComments(config_content);

    // Parse JSON
    cfg = json::parse(json_content);
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
    TIME_FUNCTION();

    bool converged = false;
    while( !converged )
    {
      performIteration();

      converged = checkConvergence();

      iteration++;
    }

  }

  void Placer::performIteration()
  {
    //    Logger::log_detail("BEGIN iteration " + std::to_string(iteration));
    //
    //    // Iteration 1: compute first gradients and initialize solver state.
    //    // Probe positions (v_1 = u_1) are already set by initializePlacement().
    //    if (iteration == 1)
    //    {
    //        iterationReset(); // is this needed?
    //
    //        computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
    //        computeElectricFields();    // ∇D from ρ → bin eFields
    //
    //        initializeDensityWeight();
    //        recordInitialHPWL();
    //    }
    //
    //    // Algorithm 1 + Algorithm 2: step with backtracking line search.
    //    // On exit: positions at u_{k+1}, v_{k+1}; gradients at v_{k+1} already computed;
    //    // nesterov_ak advanced.
    //    performNextStep(params.enable_backtracking);
    //
    //    recordIterationResults();
    //    printIterationResults();
    //
    //    updateDensityWeight();
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
  Placer::Placer(HyperParameters& params, DataBase& db) : params(params), db_(db)
  {
    printWelcomeBanner();

    pgrm_start_time = getTime();

    // Create organized output directory with timestamp and method names
    // Must be after database initialization to get benchmark name
    createRunOutputStructure();

    //params.die_size = min( grid.getDieWidth(), grid.getDieHeight() );

  }
  void Placer::printWelcomeBanner(bool show_info)
  {
//    // Raw string logo
//    string logo = R"(
//╔══════════════════════════════════════════════════════╗
//║    _____   ___               __                      ║
//║   /  _  \ │   │ _____ _____ │  │_____   ____   ____  ║
//║  /  /_\  \│   ││  __/|     \│  │\__  \ / ___\ / __ \ ║
//║ /         \   ││  _/ |  ──  │  │_/ __ \\ \___/  ___/ ║
//║ \____│____/___││____\|   __/│____\_____/\____/\____/ ║
//╠══════════════════════|  /════════════════════════════╣
//╚══════════════════════|_/═════════════════════════════╝ )";
//
//    Table banner;
//    banner.add_row({logo});
//    banner.format()
//      .width(59)
//      .hide_border()
//      .font_color(Color::white)
//      .font_align(FontAlign::left);
//
//    if(show_info)
//    {
//      Table info;
//      info.add_row({"Version:", AIEPLACE_VERSION});
//      info.format().hide_border();
//      banner.add_row({info});
//      banner.add_row({"VLSI global placement algorithm accelerated on AI Engines"});
//      banner.add_row({}); // This line intentionally left blank
//    }
//
//    banner.print(cout);
  }

  void Placer::createRunOutputStructure()
  {
//    // Generate run ID and timestamp
//    auto now = std::chrono::system_clock::now();
//    auto time_t = std::chrono::system_clock::to_time_t(now);
//    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
//        now.time_since_epoch()) % 1000;
//
//    // Create timestamp string (YYYYMMDD_HHMMSS_mmm)
//    std::stringstream timestamp_ss;
//    timestamp_ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
//    timestamp_ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
//    std::string timestamp = timestamp_ss.str();
//
//    // Create directory structure: <results_dir>/<benchmark_name>/<timestamped_run_name>/
//    output_dir = results_dir / db.getBenchmarkName() / (timestamp + "_" +
//        cfg["params"]["partials_compute_method"].get<std::string>() + "_" +
//        cfg["params"]["density_compute_method"].get<std::string>());
//
//    fs::create_directories(output_dir);
//
//    Logger::log_info("Created output directory: " + output_dir.string());
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
    // TODO: uncomment again and refactor to use new data structure
    //    float pos_norm_sq  = 0.0f;
    //    float grad_norm_sq = 0.0f;
    //
    //    auto accumulate = [&](Node* node) {
    //        // ||v̂_{k+1} - v_k||²
    //        float dx = node->next.probe_pos.x - node->current.probe_pos.x;
    //        float dy = node->next.probe_pos.y - node->current.probe_pos.y;
    //        pos_norm_sq += dx*dx + dy*dy;
    //
    //        // ||∇f(v̂_{k+1}) - ∇f(v_k)||²
    //        float dgx = node->next.probe_grad.x - node->current.probe_grad.x;
    //        float dgy = node->next.probe_grad.y - node->current.probe_grad.y;
    //        grad_norm_sq += dgx*dgx + dgy*dgy;
    //    };
    //
    //    for (auto item : db.getComponents())
    //        accumulate(item.second);
    //    for (auto filler : db.getFillers())
    //        accumulate(filler);
    //
    //    float estimate = sqrtf(pos_norm_sq) / sqrtf(grad_norm_sq + 1e-8f);
    //    Logger::log_detail("New steplength estimate: " + PREC_P(estimate, 4));
    //    return std::clamp(estimate, 0.0001f, 4000.0f);
    return 0.0001; // delete this again when refactoring
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
    //    if(iteration % 3 != 0) // only update density_weight every few iterations since HPWL can be noisy
    //    {
    //        //Logger::log_detail("Skipping density_weight update on iteration " + std::to_string(iteration));
    //        return;
    //    }
    //    float current_hpwl = hpwl_history.back();
    //    float prev_hpwl = hpwl_history[hpwl_history.size() - 2]; // safe because we have warmup iterations
    //
    //    float dw_min_step = cfg["params"]["density_weight_min_step"];
    //    float dw_max_step = cfg["params"]["density_weight_max_step"];
    //    float dw_multiplier = dw_max_step;
    //
    //    if (current_hpwl > prev_hpwl) // if HPWL is not improving,
    //    {
    //        // decrease multiplier to allow HPWL forces to have more influence
    //        float hpwl_percent_change = 100.0f * (current_hpwl - prev_hpwl) / (prev_hpwl + 1e-8f); // avoid div by 0
    //        dw_multiplier = std::max(dw_min_step, std::pow(dw_max_step, 1 - hpwl_percent_change));
    //    }
    //
    //    // perform the update
    //    density_weight *= dw_multiplier;
  }

  /**
   * @brief Reset all nodes and nets in preparation for the next iteration
   */
  void Placer::iterationReset()
  {
    //db_.iterationReset();
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
    // TODO: uncomment again and refactor to use new data structure
    //    Logger::log_trace("Begin initializePlacement()");
    //    std::srand(std::time(nullptr)); // use current time as seed for random generator
    //
    //    Table top;
    //    top.add_row(RowStream{} << "Initial Placement");
    //    Table data;
    //    data.add_row(RowStream{} << "Center" << target_pos.x << target_pos.y);
    //    data.add_row(RowStream{} << "Min dist" << min_dist);
    //    data.add_row(RowStream{} << "Max dist" << max_dist); 
    //    top.add_row({data});
    //    top.format().font_align(FontAlign::center);
    //    Logger::log_info(top);
    //
    //    float bin_area_16th = grid.getBinWidth() * grid.getBinHeight() / 16;
    //    // For each component that isn't fixed
    //    for (auto item : db.getComponents()) {
    //        // Choose a random position based on parameters
    //        // TODO: Different initial position "shapes" could help with performance?
    //        // e.g. maybe a donut shape would be good.
    //        int x_offset = min_dist + rand()%(max_dist-min_dist); // clustered around target
    //        if(rand()%2 == 1) x_offset *= -1; // 50% chance to negate
    //        int y_offset = min_dist + rand()%(max_dist-min_dist); // clustered around target
    //        if(rand()%2 == 1) y_offset *= -1; // 50% chance to negate
    //        //int x_offset = rand()%(grid.getDieWidth()) - grid.getDieWidth()/2; // Even Spread
    //        //int y_offset = rand()%(grid.getDieWidth()) - grid.getDieWidth()/2; // Even Spread
    //        Position init_pos = target_pos + Position(x_offset, y_offset);
    //        item.second->initializeState(init_pos);
    //
    //        // if this component is bigger than 1/16th of bin area, set member bool
    //        item.second->checkIfLarge(bin_area_16th);
    //    }
    //
    //    // Place Fillers
    //    for (auto filler : db.getFillers()) {
    //        int x_offset = min_dist + rand()%(grid.getDieWidth()/2); // clustered around target
    //        if(rand()%2 == 1) x_offset *= -1; // 50% chance to negate
    //        int y_offset = min_dist + rand()%(grid.getDieHeight()/2); // clustered around target
    //        if(rand()%2 == 1) y_offset *= -1; // 50% chance to negate
    //        Position init_pos = target_pos + Position(x_offset, y_offset);
    //        filler->initializeState(init_pos);
    //    }
    //
    //    // Initialize pin State (pins are fixed, but need valid probe_pos for gradient computation)
    //    for (auto item : db.getPins())
    //        item.second->initializeState(item.second->next.node_pos);
    //
    //
    //    //printIterationResults(); // Prints "iteration 0" starting statistics
    //    iteration = 1;
    //
    //    // TODO
    //    // Wild and Crazy Idea: wouldn't this have the same effect as slowly increasing the bin's lambda?
    //    // Add additional large "phantom" macros for experimentation
    //    // Observe what affect they have,
    //    // They could be made to have a repulsive affect on the real nodes or macros
    //    // These macros won't be on any nets, but they will add to the density computation
    //    // and could be created en masse at hotspot areas to gently push other nodes away.
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
    // TODO: uncomment again and refactor to use new data structure
    //    float HPWL_L1_norm = 0.0f;
    //    float density_L1_norm = 0.0f;
    //    for(auto item : db.getComponents()) {
    //        Node* node_p = item.second;
    //
    //        Gradient& partials = node_p->next.probe_grad;  // HPWL-only (before combineGradients)
    //        HPWL_L1_norm += fabs(partials.x) + fabs(partials.y);
    //
    //        Gradient electro_force = computeElectrostaticForce(node_p);
    //        density_L1_norm += fabs(electro_force.x) + fabs(electro_force.y);
    //    }
    //
    //    float initial_multiplier = cfg["params"]["density_weight_init_multiplier"];
    //    density_weight = (HPWL_L1_norm / (density_L1_norm + 1e-8f)) * initial_multiplier;
    //
    //    Logger::log_info("Initial HPWL gradient L1 norm: " + std::to_string(HPWL_L1_norm));
    //    Logger::log_info("Initial density gradient L1 norm: " + std::to_string(density_L1_norm));
    //    Logger::log_info("Initialized density_weight: " + std::to_string(density_weight));
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
    //    // Check 0: Max iterations as safety fallback
    //    if (iteration >= max_iterations) {
    //        Logger::log_info("Convergence: Reached maximum iteration " +
    //                        std::to_string(max_iterations));
    //        return true;
    //    }
    //
    //    // Check 1: Enforce minimum iterations
    //    if (iteration < min_iterations)
    //        return false;
    //
    //    // Check 2: Overflow of each bin must be below overflow threshold 
    //    bool overflow_converged = false;
    //
    //    float overflow = grid.computeTotalOverflow( 
    //                    target_density, 
    //                    db.computeTotalComponentArea());
    //
    //    Logger::log_detail("Current overflow: " + std::to_string(overflow));
    //    if (overflow < overflow_threshold) {
    //        overflow_converged = true;
    //        Logger::log_info("OVERFLOW CONVERGED (Less than: " + std::to_string(overflow_threshold) + ")");
    //    }
    //
    //    // Check 3: HPWL improvement over last N iterations must be small
    //    if (hpwl_history.size() < convergence_window + 1)
    //        return false;
    //
    //    float old_hpwl = hpwl_history[hpwl_history.size() - convergence_window - 1];
    //    float current_hpwl = hpwl_history.back();
    //    float hpwl_improvement = (old_hpwl - current_hpwl) / old_hpwl;
    //
    //    bool hpwl_converged = (hpwl_improvement < hpwl_improvement_threshold);
    //                            //&& (hpwl_improvement > 0.0f);
    //
    //    Logger::log_detail("HPWL improvement from previous "+ std::to_string(convergence_window) +
    //                    " iterations: " + std::to_string(100*hpwl_improvement) +"%");
    //    // Combined convergence: both criteria must be met
    //    if(overflow_converged && hpwl_converged) {
    //        Logger::log_info("CONVERGENCE ACHIEVED at iteration " + std::to_string(iteration));
    //
    //        return true;
    //    }
    //    else return false;
    return false;
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
    //    // Should be non-blocking and multithreaded, as partials and eFields are independent
    //    computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
    //    computeElectricFields();    // ∇D from ρ → bin eFields
    //
    //    combineGradients();         // subtract electro in-place: next.probe_grad becomes total ∇f
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
    // TODO: uncomment again and refactor to use new data structure
    //    for (auto item : db.getComponents()) {
    //        item.second->next.probe_grad -= computeElectrostaticForce(item.second);
    //    }
    //    for (auto filler : db.getFillers()) {
    //        filler->next.probe_grad -= computeElectrostaticForce(filler);
    //    }
  }


  /**
   * @brief Clamp node's actual position (m_node_pos) to the die area boundaries.
   */
  //void Placer::enforceDieBoundaries(Node* node_p)
  //{
  // TODO: uncomment again and refactor to use new data structure
  //    // Clamp node_pos
  //    node_p->next.node_pos.x = std::clamp(node_p->next.node_pos.x, 0.0f, (float)grid.getDieWidth());
  //    node_p->next.node_pos.y = std::clamp(node_p->next.node_pos.y, 0.0f, (float)grid.getDieHeight());
  //    
  //    // Clamp probe_pos
  //    node_p->next.probe_pos.x = std::clamp(node_p->next.probe_pos.x, 0.0f, (float)grid.getDieWidth());
  //    node_p->next.probe_pos.y = std::clamp(node_p->next.probe_pos.y, 0.0f, (float)grid.getDieHeight());
  //}


  /**
   * @brief Save current positions and gradients as "previous" for BB estimate and backtracking.
   * Must be called before stepAllNodes() so prev fields serve as the restore point.
   */
  void Placer::advanceIterationState()
  {
    // TODO: uncomment again and refactor to use new data structure
    //    for (auto item : db.getComponents()) item.second->cacheState();
    //    for (auto filler : db.getFillers())  filler->cacheState();
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
    // TODO: uncomment again and refactor to use new data structure
    //    for (auto item : db.getComponents()) {
    //        item.second->step(step_length, momentum_coeff);
    //        enforceDieBoundaries(item.second);
    //    }
    //    for (auto filler : db.getFillers()) {
    //        filler->step(step_length, momentum_coeff);
    //        enforceDieBoundaries(filler);
    //    }
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
    //    // Algorithm 1, Line 3: compute momentum coefficient for this iteration
    //    float a_next = (1.0f + sqrtf(4.0f * nesterov_ak * nesterov_ak + 1.0f)) / 2.0f;
    //    momentum_coeff = enable_momentum ? (nesterov_ak - 1.0f) / a_next : 0.0f;
    //    nesterov_ak = a_next;
    //
    //    // step_length (α̂) carries over from previous iteration (or warmup default)
    //    advanceIterationState();  // copy next state into current state
    //
    //    // Algorithm 2: Backtracking
    //    int tries = 0;
    //    float prev_step_length;
    //    do {
    //        // Lines 2 & 3: trial step using existing step_length from previous iteration
    //        stepAllNodes();
    //
    //        iterationReset();
    //        // Recompute gradients at trial v̂ (the expensive part, accelerated on AIEs)
    //        computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
    //        computeElectricFields();    // ∇D from ρ → bin eFields
    //        combineGradients();         // subtract electro in-place: next.probe_grad becomes total ∇f
    //
    //        prev_step_length = step_length;
    //
    //        // new steplength estimate at probe position: 
    //        // α = 1 / L = ||v̂ - v_k|| / ||∇f(v̂) - ∇f(v_k)||
    //        step_length = computeLipshitzEstimate();
    //
    //        // Accept if α̂ ≤ ε · fresh_bb (step is not too aggressive)
    //    } while(backtracking_enabled &&
    //            prev_step_length > backtrack_epsilon * step_length && // epsilon condition
    //            ++tries < max_backtracking_attempts);
    //
    //    backtrack_steps = tries; // for logging
    //
    //    if (Logger::isKeyActive("DEBUG")) logStepDiagnostics();
  }

  void Placer::logStepDiagnostics()
  {
    // TODO: uncomment again and refactor to use new data structure
    //    Logger::log_info("=== Step Diagnostics (iteration " + std::to_string(iteration) + ") ===");
    //    Logger::log_info("  step_length (α̂):  " + PREC_P(step_length, 6));
    //    Logger::log_info("  momentum_coeff:    " + PREC_P(momentum_coeff, 6));
    //    Logger::log_info("  nesterov_ak:       " + PREC_P(nesterov_ak, 6));
    //    Logger::log_info("  backtrack_steps:   " + std::to_string(backtrack_steps));
    //
    //    // Gradient statistics (from current state used for stepping)
    //    float grad_L1 = 0.0f, max_grad = 0.0f;
    //    for (auto item : db.getComponents()) {
    //        Node* n = item.second;
    //        grad_L1 += fabs(n->current.probe_grad.x) + fabs(n->current.probe_grad.y);
    //        max_grad = std::max(max_grad, std::max(fabs(n->current.probe_grad.x), fabs(n->current.probe_grad.y)));
    //    }
    //    Logger::log_info("  grad L1 norm:      " + SCI(grad_L1));
    //    Logger::log_info("  max |grad|:        " + SCI(max_grad));
    //    Logger::log_info("  α̂ * max|grad|:     " + SCI(step_length * max_grad) + "  (max single-step displacement)");
    //
    //    // Position delta statistics
    //    float max_node_delta = 0.0f, max_probe_overshoot = 0.0f;
    //    int clamped_count = 0;
    //    float die_w = (float)grid.getDieWidth(), die_h = (float)grid.getDieHeight();
    //    for (auto item : db.getComponents()) {
    //        Node* n = item.second;
    //        float dx = fabs(n->next.node_pos.x - n->current.node_pos.x);
    //        float dy = fabs(n->next.node_pos.y - n->current.node_pos.y);
    //        max_node_delta = std::max(max_node_delta, std::max(dx, dy));
    //
    //        float ox = fabs(n->next.probe_pos.x - n->next.node_pos.x);
    //        float oy = fabs(n->next.probe_pos.y - n->next.node_pos.y);
    //        max_probe_overshoot = std::max(max_probe_overshoot, std::max(ox, oy));
    //
    //        if (n->next.node_pos.x <= 0 || n->next.node_pos.x >= die_w ||
    //            n->next.node_pos.y <= 0 || n->next.node_pos.y >= die_h)
    //            clamped_count++;
    //    }
    //    Logger::log_info("  max |Δnode_pos|:   " + SCI(max_node_delta));
    //    Logger::log_info("  max probe overshoot: " + SCI(max_probe_overshoot));
    //    Logger::log_info("  nodes at boundary: " + std::to_string(clamped_count));
    //
    //    // Sample trace: first 3 components
    //    int count = 0;
    //    for (auto item : db.getComponents()) {
    //        if (count >= 3) break;
    //        Node* n = item.second;
    //        Logger::log_info("  --- Sample node: " + n->getName() + " ---");
    //        Logger::log_info("    current.node_pos (u_k):   (" + PREC_P(n->current.node_pos.x, 2) + ", " + PREC_P(n->current.node_pos.y, 2) + ")");
    //        Logger::log_info("    current.probe_pos (v_k):  (" + PREC_P(n->current.probe_pos.x, 2) + ", " + PREC_P(n->current.probe_pos.y, 2) + ")");
    //        Logger::log_info("    current.probe_grad:       (" + SCI(n->current.probe_grad.x) + ", " + SCI(n->current.probe_grad.y) + ")");
    //        Logger::log_info("    α̂ * grad:                 (" + SCI(step_length * n->current.probe_grad.x) + ", " + SCI(step_length * n->current.probe_grad.y) + ")");
    //        Logger::log_info("    next.node_pos (u_{k+1}):  (" + PREC_P(n->next.node_pos.x, 2) + ", " + PREC_P(n->next.node_pos.y, 2) + ")");
    //        Logger::log_info("    next.probe_pos (v_{k+1}): (" + PREC_P(n->next.probe_pos.x, 2) + ", " + PREC_P(n->next.probe_pos.y, 2) + ")");
    //        Logger::log_info("    next.probe_grad:          (" + SCI(n->next.probe_grad.x) + ", " + SCI(n->next.probe_grad.y) + ")");
    //        count++;
    //    }
    //    Logger::log_info("=== End Step Diagnostics ===");
  }

}
