#include "DCT.h"
#include "AIEplace.h"
#ifdef USE_TBB
#include <tbb/tbb.h>
#endif
#include <cmath>
#include <cassert>

AIEPLACE_NAMESPACE_BEGIN

/* @brief: perform an entire iteration of the ePlace algorithm.
*/
void Placer::performIteration()
{
    Logger::log_detail("BEGIN iteration " + std::to_string(iteration));

    iterationReset();

    // Compute terms for HPWL partials
    if(partials_method == "aie") {
#ifdef USE_XILINX_XRT
        computeAllPartials_AIE();
#else
        Logger::log_error("partials_method 'aie' requires XRT. Recompile with BUILD_XRT=1 or use 'cpu'/'simple'");
        exit(1);
#endif
    }
    else if(partials_method == "cpu") {
        computeAllPartials_CPU();
    }
    else if(partials_method == "simple") {
        computeAllPartials_simple();
    }
    else if(partials_method == "orig") {
#ifdef USE_TBB
        computeAllPartials_CPU_orig();
#else
        Logger::log_error("partials_method 'orig' requires TBB. Recompile with -DUSE_TBB or use 'cpu'/'simple'");
        exit(1);
#endif
    }
    else if(partials_method == "hybrid") {
        //computeAllPartials_CPU_hybrid();
    }
    else if(partials_method == "threaded") {
        //computeAllPartials_ThreadSafe();
    }
    else { 
        Logger::log_error("Invalid partials_compute_method specified in config file"); 
        exit(1);
    }

    // Compare results to ensure correctness
    //computeAllPartials_CPU();
    //comparePartialResults();

    // Compute Electric Fields in each bin
    computeOverlaps(); // Density Map computation
    //db.printOverlaps();
    //grid.printOverflows();

    if(density_method == "aie") {
#ifdef USE_XILINX_XRT
        computeElectricFields_AIE(); // Accelerated compute on AIEs
#else
        Logger::log_error("density_method 'aie' requires XRT. Recompile with BUILD_XRT=1 or use 'cpu'");
        exit(1);
#endif
    } else if(density_method == "cpu") {
        //computeElectricFields_CPU(); // Compute E-fields using naive algorithm 
        computeElectricFields_DCT(); // Compute E-fields on CPU using DCT for verification
    }
    //normalizeElectricFields();
    //computeElectricFields_DCT(); // Compute E-fields on CPU using DCT for verification
    //placer.grid.printElectricFields();

    // Perform iteration node movement
    nudgeAllNodes();
    printIterationResults();
}

/* @brief: Run the ePlace algorithm.
*          Perform iterations until the convergence condition is met.
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

    recordInitialHPWL();

    bool converged = false;
    while( !converged )
    {
        TIME_BLOCK("Algorithm Block");


        updateHyperparameters();

        performIteration();
        
        
        // check for convergence
        // TODO: need to actually check for convergence instead of running to max iterations
        if (iteration >= cfg["params"]["max_iterations"])
            converged = true;
        else iteration++;
    }

    plotHistories();
    algo_time = getInterval(algo_start, getTime());
}

/* @brief: Implement dynamic adpatation of hyperparameters
*/
void Placer::updateHyperparameters()
{
        // Option 3: Multi-phase approach
        if(iteration < 10) learning_rate = 1000;        // Exploration
        else if(iteration < 50) learning_rate = 100;   // Transition  
        else if(iteration < 200) learning_rate = 10;   // Transition  
        else if(iteration < 400) learning_rate = 1;   // Transition  
        else if(iteration < 700) learning_rate = .1;   // Transition  
        else learning_rate *= .01;                    // Refinement


        // SIMPLEST APPROACH
        // Update hyperparameters for new iteration
        // every 100 iterations, slow learning rate
        //if(iteration % 100 == 0)
        //    learning_rate *= 0.8;

        //// every 10 iterations, bump up lambda (density weighting)
        if(iteration >= 50 && iteration % 10 == 0)
            global_lambda *= 1.1;

        global_lambda = std::min(global_lambda, 50.0f); // cap lambda at 100

}

/* @brief: Reset all nodes and nets in preparation for the next iteration.
*/
void Placer::iterationReset()
{
    grid.iterationReset();
    db.iterationReset();

    all_partials.clear();
    simple_partials.clear();
}

// Constructor
Placer::Placer(std::string config_filepath ) 
        { 
            // Read configuration JSON file
            Logger::log_info("Reading runtime configuration from: " + config_filepath);
            std::ifstream config_file(config_filepath);
            // check if config file was found
            if (!config_file.is_open()) {
                Logger::log_error("Unable to open configuration JSON file: " + config_filepath);
                exit(1);
            }

            pgrm_start_time = getTime();

            cfg = json::parse(config_file);

            //initialize values from JSON
            partials_method = cfg["params"]["partials_compute_method"];
            density_method = cfg["params"]["density_compute_method"];
            Logger::log_info("Partials compute method: " + partials_method);
            Logger::log_info("Density compute method:  " + density_method);

            gamma = cfg["params"]["gamma"];
            inv_gamma = 1.0f / gamma;
            learning_rate = cfg["params"]["init_learning_rate"];
            global_lambda = cfg["params"]["init_global_lambda"];
            MAX_THREADS = cfg["params"]["max_threads"];
            input_dir = fs::path(cfg["input"]["benchmark"]);
            output_dir = getOutputPath();
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

            db_IO_time = getInterval(pgrm_start_time, getTime());
            Logger::log_info("db read time: " + std::to_string(db_IO_time));
            grid = Grid(db.getDieArea(), BINS_PER_ROW, BINS_PER_ROW); 

            die_size = min( grid.getDieWidth(), grid.getDieHeight() );

            #ifdef CREATE_VISUALIZATION
                if(cfg["output"]["visualize"])
                    viz.init(db.getDieArea());
            #endif
        }

void Placer::printWelcomeBanner()
{
    // Raw string logo
    string logo = R"(
╔══════════════════════════════════════════════════════╗
║    _____   ___               __                      ║
║   /  _  \ │   │ _____ _____ │  │_____   ____   ____  ║
║  /  /_\  \│   ││  __/|     \│  │\__  \ / ___\ / __ \ ║
║ /         \   ││  _/ |  ──  │  │_/ __ \\ \___/  ___/ ║
║ \____│____/___││____\|   __/│____\_____/\____/\____/ ║
╠══════════════════════|  /════════════════════════════╣
╚══════════════════════|_/═════════════════════════════╝ )";

    Table banner;
    banner.add_row({logo});
    banner.format()
        .width(59)
        .hide_border()
        .font_color(Color::white)
        .font_align(FontAlign::left);

    Table info;
    info.add_row({"Version:", AIEPLACE_VERSION});
    info.format().hide_border();
    banner.add_row({info});
    banner.add_row({"VLSI global placement algorithm accelerated on AI Engines"});
    banner.add_row({}); // This line intentionally left blank

    banner.print(cout);
}

/* @brief: initialize placement of all moveable nodes randomly,
 *          clustered about the target position
 * @param: target_pos: position around which nodes are spread
 * @param: min_dist: minimum distance from target_pos a node can appear
 * @param: max_dist: maximum distance from target_pos a node can appear
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
    printIterationResults(); // Prints "iteration 0" starting statistics
    iteration = 1;

    // TODO
    // Wild and Crazy Idea: wouldn't this have the same effect as slowly increasing the bin's lambda?
    // Add additional large "phantom" macros for experimentation
    // Observe what affect they have,
    // They could be made to have a repulsive affect on the real nodes or macros
    // These macros won't be on any nets, but they will add to the density computation
    // and could be created en masse at hotspot areas to gently push other nodes away.
}

/***************
 * XRT/AIE ACCELERATION FUNCTIONS - VCK5000 only
 *
 * These functions are only compiled when BUILD_XRT environment variable is set.
 * They provide hardware-accelerated computation on Versal AI Engines via XRT.
 *
 * Partials functions moved to Partials.cpp
 * Density functions moved to Density.cpp
 ****************/

#ifdef USE_XILINX_XRT

// XRT-accelerated functions moved to respective files

#endif // USE_XILINX_XRT


/***************
 * CPU FUNCTIONS - Always available
 *
 * These functions run on the host CPU and don't require XRT or VCK5000 hardware.
 * Partials functions moved to Partials.cpp
 * Density functions moved to Density.cpp
 ****************/



void Placer::nudgeAllNodes()
{
    //Logger::log_detail("Begin nudgeAllNodes()");
    for (auto item : db.getComponents())
        nudgeNode(item.second);
}

void Placer::nudgeNode(Node* node_p)
{
    XY electro_force;
    electro_force.clear(); // set XY to 0

    // for each bin that this node overlaps,
    // compute electric force based on bin overlaps
    for (BinOverlap b : node_p->getBinOverlaps()) {
        Bin* bin = b.bin;
        // add electric force
        // What does ePlace do for this step?
        float coeff = global_lambda * bin->lambda * b.overlap/bin->bb.getArea();
        electro_force.x += coeff * bin->eField.x;
        electro_force.y += coeff * bin->eField.y;
    }


    float partials_x, partials_y; 
    if(partials_method == "aie") {
        partials_x = node_p->partials_aie.x;
        partials_y = node_p->partials_aie.y;
    } else {
        partials_x = node_p->terms_cpu.partials.x;
        partials_y = node_p->terms_cpu.partials.y;
    }

    XY move;
    // coeff is the learning rate scaled by the size of the die
    //float x_coeff = learning_rate * grid.getDieWidth();
    //float y_coeff = learning_rate * grid.getDieHeight();

    move.x = learning_rate * (electro_force.x - partials_x ); // we subtract the partials to reduce net size!
    move.y = learning_rate * (electro_force.y - partials_y );

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

void Placer::printIterationResults()
{
    float hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"]);
    hpwl_history.push_back(hpwl);

    float learning_coeff = learning_rate * die_size;
    learning_coeff_history.push_back(learning_coeff);

    float overflow = grid.computeTotalOverflow();
    //if(iteration >=10)
    ovfw_history.push_back(overflow);

    //Logger::log_data("HPWL = " + std::to_string(hpwl));
    // every 10 iterations, export a table in markdown
    if (iteration % 10 == 0)
    {
        Table top;
        top.add_row(RowStream{} << "Iteration" << iteration);
        top.add_row(RowStream{} << "HPWL" << hpwl);
        top.add_row(RowStream{} << "Overflow" << overflow);
        top.add_row(RowStream{} << "Learning Rate" << learning_rate);
        top.add_row(RowStream{} << "Learning Coeff" << learning_rate * die_size);
        top.add_row(RowStream{} << "Global Lambda" << global_lambda);
        top.column(0).format().font_align(FontAlign::right);
        top.column(1).format().font_align(FontAlign::left);
        Logger::log_data(top);
    }

    // every 10 iterations, export an image
    #ifdef CREATE_VISUALIZATION
        if(cfg["output"]["visualize"])
        if (iteration % int(cfg["output"]["iterations_per_export"]) == 0) {
            viz.drawPlacement(db, output_dir / "placement", iteration);
            viz.drawElectricField(grid, output_dir / "efield", iteration);
        }
    #endif

    //Logger::export_eField(grid, output_dir, iteration);

    // Append the HPWL value to a file for later analysis
    std::ofstream hpwl_file;
    fs::path dir = output_dir;
    hpwl_file.open(dir.append("hpwl.dat"), std::ios_base::app);
    if(iteration == 0) {
        hpwl_file << "Iter, HPWL, OVFW, LR, LAMBDA" << endl; // Write header only for the first iteration
    }
    hpwl_file << std::setfill('0') << std::setw(3) << iteration << ", " 
              << std::setprecision(2) << std::scientific << hpwl << ", " 
              << std::setprecision(2) << std::scientific << overflow << ", " 
              << std::setprecision(4) << std::fixed << learning_rate << ", "
              << std::setprecision(4) << std::fixed << global_lambda << endl;
    hpwl_file.close();
}

void Placer::plotHistories() {
#ifdef CREATE_VISUALIZATION
    fs::path data_dir = output_dir / "data";
    if (!fs::exists(data_dir))
        fs::create_directories(data_dir);

    // Create individual plots
    CairoPlotter hpwl_plotter(800, 600);
    hpwl_plotter.plotHistory(hpwl_history, "HPWL Convergence", "HPWL Value", 0.0, 0.5, 1.0);
    hpwl_plotter.savePNG(data_dir / "hpwl_history.png");

    CairoPlotter ovfw_plotter(800, 600);
    ovfw_plotter.plotHistory(ovfw_history, "Overflow", "OVFW Value", 0.0, 0.5, 1.0);
    ovfw_plotter.savePNG(data_dir / "ovfw_history.png");

    CairoPlotter coeff_plotter(800, 600);
    coeff_plotter.plotHistory(learning_coeff_history, "Learning Coefficient History", "Learning Coefficient", 1.0, 0.2, 0.2);
    coeff_plotter.savePNG(data_dir / "learning_coeff_history.png");

    // Create combined plot
    CairoPlotter dual_plotter(800, 600);
    dual_plotter.plotDualHistory(hpwl_history, ovfw_history,
                                "HPWL and Overflow History",
                                "HPWL (normalized)", "Overflow (normalized)");
    dual_plotter.savePNG(data_dir / "combined_history.png");
#endif
}

// Enhanced function to create organized output structure
void Placer::createRunOutputStructure(std::string& run_output_dir, std::string& run_id)
{
    // Generate run ID and timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    // Create timestamp string (YYYYMMDD_HHMMSS_mmm)
    std::stringstream timestamp_ss;
    timestamp_ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    timestamp_ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    std::string timestamp = timestamp_ss.str();
    
    // Generate run ID
    run_id = db.getBenchmarkName() + "_" + timestamp;
    
    // Create directory structure: ./results/<benchmark_name>/<timestamped_run_name>/
    fs::path results_base("results");
    fs::path benchmark_dir = results_base / db.getBenchmarkName();
    fs::path run_dir = benchmark_dir / (timestamp + "_" + 
                                       cfg["params"]["partials_compute_method"].get<std::string>() + "_" +
                                       cfg["params"]["density_compute_method"].get<std::string>());
    
    if (!fs::exists(run_dir)) {
        fs::create_directories(run_dir);
    }
    
    run_output_dir = run_dir.string();
    
    Logger::log_info("Created run directory: " + run_output_dir);
}

// Enhanced printFinalResults with organized output structure
void Placer::printFinalResults()
{
    Logger::log_info("AIEplace algorithm complete.");
    
    // Create organized output structure
    std::string run_output_dir, run_id;
    createRunOutputStructure(run_output_dir, run_id);
    
    // Calculate final metrics
    float final_hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"]);
    float final_overflow = grid.computeTotalOverflow();
    float total_runtime = getInterval(pgrm_start_time, getTime());
    float iteration_avg = (iteration > 0) ? total_runtime / iteration : 0.0f;
    
    // Calculate HPWL improvement if initial HPWL was recorded
    float hpwl_improvement = 0.0f;
    bool has_improvement = false;
    if (initial_hpwl > 0) {
        hpwl_improvement = ((initial_hpwl - final_hpwl) / initial_hpwl) * 100.0f;
        has_improvement = true;
    }
    
    // Traditional table output (existing functionality) - still to console
    Table statistics;
    statistics.add_row({"AIEplace Run Statistics"});
    
    Table results;
    results.add_row({"Benchmark name", db.getBenchmarkName()});
    results.add_row(RowStream{} << "Iterations" << iteration);
    results.add_row(RowStream{} << "Total runtime (s)" << std::fixed << std::setprecision(3) << total_runtime);
    results.add_row(RowStream{} << "Database I/O time (s)" << std::fixed << std::setprecision(3) << db_IO_time);
    results.add_row(RowStream{} << "Algorithm time (s)" << std::fixed << std::setprecision(3) << algo_time);
    results.add_row(RowStream{} << "Avg iteration time (s)" << std::fixed << std::setprecision(3) << iteration_avg);
    results.add_row(RowStream{} << "Final HPWL" << std::scientific << std::setprecision(3) << final_hpwl);
    if (has_improvement) {
        results.add_row(RowStream{} << "Initial HPWL" << std::scientific << std::setprecision(3) << initial_hpwl);
        results.add_row(RowStream{} << "HPWL improvement (%)" << std::fixed << std::setprecision(2) << hpwl_improvement);
    }
    results.add_row(RowStream{} << "Final Overflow" << std::scientific << std::setprecision(3) << final_overflow);
    results.column(0).format().font_align(FontAlign::right);
    results.column(1).format().font_align(FontAlign::left);
    
    Table hyperparams;
    hyperparams.add_row({"Hyperparameter", "Final Value"});
    hyperparams.add_row(RowStream{} << "gamma" << gamma);
    hyperparams.add_row(RowStream{} << "learning rate" << learning_rate);
    hyperparams.add_row(RowStream{} << "partials method" << cfg["params"]["partials_compute_method"]);
    hyperparams.add_row(RowStream{} << "density method" << cfg["params"]["density_compute_method"]);
    hyperparams.add_row(RowStream{} << "wirelength method" << cfg["params"]["wirelength_method"]);
    hyperparams.column(0).format().font_align(FontAlign::right);
    hyperparams.column(1).format().font_align(FontAlign::left);
    
    statistics.format().font_align(FontAlign::center);
    statistics.add_row({results});
    statistics.add_row({hyperparams});
    
    Logger::log_data(statistics);
    
    // Export markdown to run-specific directory
    Logger::export_markdown(statistics, run_output_dir, "run_summary");
    
    // Function statistics to run-specific directory
    Table function_stats = Logger::printFunctionStats();
    Logger::export_markdown(function_stats, run_output_dir, "function_statistics");
    
    // Enhanced CSV export to global results.csv
    Logger::ProgramStatBlock stats;
    populateStatsBlock(stats, final_hpwl, final_overflow, total_runtime, 
                      iteration_avg, hpwl_improvement, has_improvement, run_id);
    
    Logger::append_csv(stats, cfg["output"]["result_csv"]);
    
    // Generate visualization in run-specific directory
    #ifdef CREATE_VISUALIZATION
        if(cfg["output"]["visualize"])
            viz.drawPlacement(db, run_output_dir, iteration);
    #endif
    
    // Write placed design to DEF in run-specific directory
    db.writeDEF(run_output_dir);
    
    // Copy config file to run directory for reproducibility
    std::string config_backup = run_output_dir + "/config_used.json";
    std::ifstream src("host/run_config.json");
    std::ofstream dst(config_backup);
    dst << src.rdbuf();
    
    Logger::log_info("All outputs saved to: " + run_output_dir);
}

// Helper function to populate the comprehensive stats block
void Placer::populateStatsBlock(Logger::ProgramStatBlock& stats, 
                               float final_hpwl, float final_overflow, 
                               float total_runtime, float iteration_avg,
                               float hpwl_improvement, bool has_improvement,
                               const std::string& run_id)
{
    // Generate timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp_ss;
    timestamp_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    
    // Basic information
    stats.timestamp = timestamp_ss.str();
    stats.run_id = run_id;
    stats.design_name = db.getBenchmarkName();
    int net_count = db.getNetsVector().size();
    std::string category;
    if (net_count < 100000) {
        category = " (Small)";
    } else if (net_count < 500000) {
        category = " (Medium)";
    } else if (net_count < 1000000) {
        category = " (Large)";
    } else {
        category = " (XLarge)";
    }
    stats.benchmark_size = std::to_string(net_count) + category;
    
    // Configuration
    stats.partials_method = cfg["params"]["partials_compute_method"];
    stats.density_method = cfg["params"]["density_compute_method"];
    stats.wirelength_method = cfg["params"]["wirelength_method"];
    stats.gamma = gamma;
    stats.init_learning_rate = cfg["params"]["init_learning_rate"];
    stats.max_iterations = cfg["params"]["max_iterations"];
    
    // Results
    stats.iteration_count = iteration;
    stats.final_hpwl = final_hpwl;
    stats.initial_hpwl = initial_hpwl;
    stats.hpwl_improvement = hpwl_improvement;
    stats.has_improvement = has_improvement;
    stats.final_overflow = final_overflow;
    stats.final_learning_rate = learning_rate;
    stats.convergence_reached = checkConvergence();
    
    // Timing
    stats.prgm_runtime = total_runtime;
    stats.db_IO_time = db_IO_time;
    stats.algo_time = algo_time;
    stats.iteration_avg_time = iteration_avg;
    
    // System metrics
    stats.memory_usage_mb = getMemoryUsageMB();
    
    // Status
    stats.success = true;  // If we got here, it succeeded
    stats.error_message = "";
}

// Helper function to escape JSON strings
std::string Placer::escapeJsonString(const std::string& input)
{
    std::string output;
    output.reserve(input.length() + 10); // Reserve some extra space for escapes
    
    for (char c : input) {
        switch (c) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b";  break;
            case '\f': output += "\\f";  break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            default:
                if (c < 0x20) {
                    output += "\\u";
                    output += "0000";
                    // Simple hex conversion for control characters
                    char hex[3];
                    sprintf(hex, "%02x", (unsigned char)c);
                    output[output.length() - 2] = hex[0];
                    output[output.length() - 1] = hex[1];
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

// Helper function to generate unique run ID
std::string Placer::generateRunId()
{
    // Simple run ID based on timestamp and benchmark
    auto now = std::chrono::high_resolution_clock::now();
    auto epoch = now.time_since_epoch();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch);
    
    std::stringstream run_id;
    run_id << db.getBenchmarkName() << "_" << nanoseconds.count() % 1000000;
    return run_id.str();
}


// Helper function to check convergence (implement based on your criteria)
bool Placer::checkConvergence()
{
    // Implement your convergence checking logic here
    // For now, simple check if we completed all iterations
    return iteration >= cfg["params"]["max_iterations"];
}


#ifdef __linux__
// This implementation only works on Linux systems
float Placer::getMemoryUsageMB()
{
    std::ifstream status_file("/proc/self/status");
    std::string line;
    
    while (std::getline(status_file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line);
            std::string label, size_str, unit;
            iss >> label >> size_str >> unit;
            
            float size_kb = std::stof(size_str);
            return size_kb / 1024.0f;  // Convert KB to MB
        }
    }
    return 0.0f;  // Not found
}
#endif

// Additional function to track initial HPWL (call this at the start of placement)
void Placer::recordInitialHPWL()
{
    initial_hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"]);
    Logger::log_info("Initial HPWL recorded: " + std::to_string(initial_hpwl));
}

fs::path Placer::getOutputPath()
{
    std::time_t time = std::time(0);   // get time now
    std::tm* now = std::localtime(&time);

    std::stringstream ss;
    ss << "run_" <<  now->tm_yday+1 << "_" << now->tm_hour << ":" << now->tm_min;

    fs::path dir = "results";
    dir.append(input_dir.filename().string());
    dir.append(ss.str());
    fs::create_directories(dir); // ensure this directory exists

    return dir;
}

void Placer::initializeFocus()
{
    // add named focus nets
    //for()

    // add random focus nets
    auto nets = db.getNets();
    auto iter = nets.begin();
    for(int i = 0; i < cfg["params"]["rand_focus_nets"]; i++) {
        // pick a random net to focus which has a pin
        //std::advance(iter, rand() % nets.size());
        while(!iter->second->hasPin())
            std::advance(iter, 1);
        db.addFocusNet(iter->second);
        std::advance(iter, 1);
    }

    auto nodes = db.getComponents();
    for(int i = 0; i < cfg["params"]["rand_focus_nodes"]; i++) {
        // pick a random node to focus that isn't a primary IO pin
        auto node_iter = nodes.begin();
        std::advance(node_iter, rand() % nodes.size());
        db.addFocusNode(node_iter->second);
    }
}


AIEPLACE_NAMESPACE_END
