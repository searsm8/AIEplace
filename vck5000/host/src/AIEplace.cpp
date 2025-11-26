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



AIEPLACE_NAMESPACE_END
