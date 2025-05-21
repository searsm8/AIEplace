#include "AIEplace.h"
#include "DCT.h"
#include <cmath>
#include <tbb/tbb.h>

AIEPLACE_NAMESPACE_BEGIN

/* @brief: perform an entire iteration of the ePlace algorithm.
*/
void Placer::performIteration()
{
    iterationReset();

    // launch threads from this function?

    // Compute terms for HPWL partials
    if(params["use_aie_partials"]) {
        computeAllPartials_AIE();
    } else {
        computeAllPartials_CPU();
    }

    // Compare results to ensure correctness
    //computeAllPartials_CPU();
    //comparePartialResults();

    // Compute Electric Fields in each bin
    computeOverlaps(); // Density Map computation
    //db.printOverlaps();
    //grid.printOverflows();

    if(params["use_aie_density"]) {
        computeElectricFields_AIE(); // Accelerated compute on AIEs
    } else {
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
    algo_start = getEpoch();
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
        // Update hyperparameters for new iteration
        // every 100 iterations, slow learning rate
        if(iteration % 100 == 0)
            learning_rate *= 0.8;

        // every 10 iterations, bump up lambda (density weighting)
        if(iteration % 10 == 0)
            global_lambda *= 1.01;

        performIteration();
        
        // check for convergence
        // TODO: need to actually check for convergence instead of running to max iterations
        if (iteration >= params["max_iterations"])
            converged = true;
        else iteration++;
    }
    algo_time = getTiming(getEpoch(), algo_start);
}

/* @brief: Reset all nodes and nets in preparation for the next iteration.
*/
void Placer::iterationReset()
{
    grid.iterationReset();
    db.iterationReset();
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

            pgrm_start_time = getEpoch();

            json config = json::parse(config_file);
            params = config["params"];

            //initialize values from JSON
            gamma = params["gamma"];
            learning_rate = params["init_learning_rate"];
            global_lambda = params["init_global_lambda"];
            MAX_THREADS = params["max_threads"];
            input_dir = fs::path(params["input_filepath"]);
            output_dir = getOutputPath();
            string xclbin_file = params["xclbin"];

            if(params["use_aie_partials"] || params["use_aie_density"]) {
                TIME_BLOCK("AIE setup");
                // Open Xilinx Device
                xrt::device device = xrt::device(DEVICE_ID);
                Logger::log_info("Device found -- ID: " + std::to_string(DEVICE_ID));

                // Load xclbin which includes PL and AIE graph
                Logger::log_info("Loading xclbin: \"" + xclbin_file + "\"");
                xrt::uuid xclbin_uuid = device.load_xclbin(xclbin_file);
                Logger::log_info("Success!");

                if(params["use_aie_partials"]) {
                    // Create drivers which handle buffer IO
                    for(int i = 0; i < PARTIALS_GRAPH_COUNT; i++)
                        partials_drivers[i].init(device, xclbin_uuid, i);
                }
                    
                if(params["use_aie_density"]) {
                    density_driver[0].init(device, xclbin_uuid, 0, BINS_PER_ROW); // DCT graph
                    density_driver[1].init(device, xclbin_uuid, 1, BINS_PER_ROW); // IDCT graph
                    density_driver[2].init(device, xclbin_uuid, 2, BINS_PER_ROW); // IDXST graph
                }
            }

            // Initialize database by reading LEF and DEF design files
            db = DataBase(input_dir); // TODO: Database initialization should be multithreaded?

            db_IO_time = getTiming(getEpoch(), pgrm_start_time);
            Logger::log_info("db read time: " + std::to_string(db_IO_time));
            grid = Grid(db.getDieArea(), BINS_PER_ROW, BINS_PER_ROW); 

            #ifdef CREATE_VISUALIZATION
                if(params["visualize"])
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

bool isClose(float a, float b)
{
    float diff = abs((a) - (b));
    if((diff < MIN_TOL) || ((diff / abs(a)) < MIN_TOL))
        return true;
    return false;
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

    // TODO
    // Wild and Crazy Idea: wouldn't this have the same effect as slowly increasing the bin's lambda?
    // Add additional large "phantom" macros for experimentation
    // Observe what affect they have,
    // They could be made to have a repulsive affect on the real nodes or macros
    // These macros won't be on any nets, but they will add to the density computation
    // and could be created en masse at hotspot areas to gently push other nodes away.

    // First, we create the phantom Macroclass with large size

   // MacroClass* new_macro = new MacroClass(m.name(), m.sizeX(), m.sizeY());
   // mm_macros.emplace(std::make_pair(m.name(), new_macro));

   // // Next, the components with "large" dimensions, such as 1/2 bin size
   // Component* new_comp = new Component("phantom");
   // new_comp->setMacroClass(mm_macros[c.macro_name]);
   // new_comp->setPlacementStatus(c.status);
   // new_comp->setPosition(Position((position_type)c.origin[0], (position_type)c.origin[1]));
   // mm_components.emplace(std::make_pair(new_comp->getName(), new_co

   // // Finally, we scatter these extra macros around the die layout


    printIterationResults(); // Prints "iteration 0" starting statistics
    iteration = 1;
}

/***************
 * AIE acceleration functions
****************/

/*
 * @brief On AIEs, compute partial derivatives
 *
**/
#define GROUP_SIZE 1 // Size of the group of nets sent before waiting to receive results
void Placer::computeAllPartials_AIE()
{
    TIME_FUNCTION();
    Logger::log_trace("BEGIN computeAllPartials_AIE()");

    // for each packet specified in DataBase
    for(int packet_index {0}; packet_index < db.getPacketCount(); packet_index++) {
        TIME_BLOCK("packet block");
        int graphs_active {0};
        // send a packet to each AIE graph
        long start_prep {getTime()};

        std::vector<std::thread> partials_threads;
        for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
            //cout << "packet_index: " << packet_index << "\t < " << db.mv_packet[graph_index].size() << endl;
            if(packet_index < db.mv_packet[graph_index].size()) {
                //cout << "computePartials on graph " << db.mv_packet[graph_index][packet_index]->graph_index
                //    << "\t" << db.mv_packet[graph_index][packet_index]->contents[0].to_string();

                partials_threads.emplace_back(&AIEplace::Placer::computePartials, this, db.mv_packet[graph_index][packet_index]);
                //partials_threads[graph_index].detach();

                //computePartials(db.mv_packet[graph_index][packet_index]);

                // Need to tell each thread starting offset, and packets_per_graph to know how far to go
                //partials_threads[graph_index] = std::thread([this, graph_index, packet_index]() {
                //    this->computePartials(db.mv_packet[graph_index][packet_index]);
                //});

                graphs_active++;
            }
        }

        // Join threads
        for(auto& thread : partials_threads) {
            thread.join();
        }

        // receive output from each AIE graph
        Timer t_receive{};
        for(int graph_index = 0; graph_index < graphs_active; graph_index++) {
            if(packet_index < db.mv_packet[graph_index].size()) {
                //cout << endl << "Receiving partials for graph " << graph_index << endl;
                receivePartials(db.mv_packet[graph_index][packet_index]);
                //partials_threads[graph_index] = std::thread([this, graph_index, packet_index]() {
                //    this->receivePartials(db.mv_packet[graph_index][packet_index]);
                //});
            }
        }
        Logger::updateFunctionStats("receiving_packets", t_receive.stop());

        // Join threads
        //for(int graph_index = 0; graph_index < graphs_active; graph_index++) {
        //    //cout << "Joining thread: " << graph_index << endl;
        //    partials_threads[graph_index].join();
        //}
    }

//        for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//            cout << "Joining thread: " << graph_index << endl;
//            partials_threads[graph_index].join();
//        }
    Logger::log_trace("END computeAllPartials_AIE()");
}

// Send a packet of coordinate data to the AIE partials computation graph
void Placer::computePartials(Packet* p)
{
    TIME_FUNCTION();
    Logger::log_trace("BEGIN computePartials(Packet* p)");
    float * input_packet = new float[INPUT_PACKET_SIZE]; // extra size for ctrl data

    // set ctrl data for the packet
    int index = 0;
    for(PacketIndex pind : p->contents) {
        input_packet[index++] = pind.net_size;
        input_packet[index++] = pind.group_count;
    }
    while(index < 8)
        input_packet[index++] = 0;

    
    long start = getTime();
    for(PacketIndex pind : p->contents) {
        // fetch the current packet's postion data into a float* array (with ctrl data)
        //cout << "Preparing net groups: " << pind.group_start << " thru " << pind.group_start + pind.group_count << endl;
        for(int group_index = pind.group_start; group_index < pind.group_start + pind.group_count; ++group_index) {
            db.prepareNetGroup(input_packet, pind.net_size, group_index*NETS_PER_GROUP );
        }
    }

    // send the data packet to PL (maybe as a thread?)
    partials_drivers[p->graph_index].send_packet(input_packet);

    Logger::log_trace("END computePartials(Packet* p)");
}

// Receive the result and place it into the database appropriately
void Placer::receivePartials(Packet* p)
{
    TIME_FUNCTION();
    Logger::log_trace("BEGIN receivePartials(Packet* p)");
    //cout << "*receivePartials on graph " << p->graph_index  << "\t" << p->contents[0].to_string();

    // receive the result data packet from PL
    float * output_packet = new float[OUTPUT_PACKET_SIZE];
    partials_drivers[p->graph_index].receive_packet(output_packet);

    // DEBUG: print output packet
    //cout << "output_packet:" << endl;
    //for( int i = 0; i < LCM_BUFFSIZE*VEC_SIZE; i++) {
    //    if(i%8 == 0) cout << endl;
    //    //if(i%8*TEST_NET_SIZE == 0) cout << endl;
    //    cout << output_packet[i] << " ";
    //}

    // store it into database, updating node partials
    for(PacketIndex pind : p->contents) {
        for(int group_index = pind.group_start; group_index < pind.group_start + pind.group_count; ++group_index) {
            int nan_count  = db.storeNetGroup(output_packet, pind.net_size, group_index*NETS_PER_GROUP);
            if(nan_count > 0) {
                Logger::log_critical("NaN result detected...exiting");
                Logger::ProgramStatBlock stats;
                stats.design_name = db.getBenchmarkName();
                stats.iteration_count = iteration;
                stats.final_hpwl = 0; // used to denote error
                stats.final_learning_rate = 0;
                stats.prgm_runtime = 0;
                stats.db_IO_time = 0;
                stats.algo_time = 0;
                stats.AIE_time = 0;
                Logger::append_csv(stats);
                exit(1);
            }

        }
    }
    Logger::log_trace("END receivePartials(Packet* p)");
}


/*
 * @brief On AIEs, compute Electric fields using 2D-DCT method
 *
**/
void Placer::computeElectricFields_AIE()
{
    Logger::log_trace("Begin computeElectricFields_AIE()");

    // Call AIE graph_driver to accelerate computation
    std::vector< std::vector<float> > rho = grid.getRho();
    std::vector< std::vector<float> > temp;

    //DEBUGGING: print out the rho matrix
    //for( int x_index = 0; x_index < BINS_PER_ROW; x_index++)
    //{
    //    for( int y_index = 0; y_index < BINS_PER_ROW; y_index++)
    //    {
    //        cout << rho[x_index][y_index] << " ";
    //    }
    //    cout << endl;
    //}
    //cout << endl;

    float * input_data  = new float[2*BINS_PER_ROW]; 
    float * output_data = new float[2*BINS_PER_ROW];

    // Send the rho matrix into the AIE, one row at a time, for 1D-DCTs

    for(int row = 0; row < BINS_PER_ROW; row++) {
        for(int col = 0; col < BINS_PER_ROW; col++) {
        input_data[2*col] = rho[row][col]; // real part
        input_data[2*col+1] = 0; // imaginary part
        }

    density_driver[0].send_packet(input_data);
    density_driver[0].receive_packet(output_data);

    std::vector<float> res;
    for(int col = 0; col < BINS_PER_ROW; col++)
        res.push_back(output_data[2*col]);
    temp.push_back(res);

    //std::vector<float> test_output = DCT_naive(test_data);
    //for(int i = 0; i < BINS_PER_ROW; i++) {
    //    cout << test_output[i] << " ";
    //} cout << endl;

    } 

    // Send the rho matrix into the AIE, one column at a time, to complete 2D-DCT
    //cout << "Input" << std::setprecision(2) << endl;
    for(int col = 0; col < BINS_PER_ROW; col++) {
        for(int row = 0; row < BINS_PER_ROW; row++) { // looping order performs DCT on columns
        input_data[2*row] = temp[row][col];
        input_data[2*row+1] = 0;
        }

        // Send data to DCT graph
        density_driver[0].send_packet(input_data);
        density_driver[0].receive_packet(output_data);

        //cout << endl << "AIE DCT output:" << endl << std::setprecision(2);
        // Store the result a_uv transposed (for comparison)
        for(int row = 0; row < BINS_PER_ROW; row++) {
            grid.getBin(row, col).a_uv = output_data[2*row];
            //cout << output_data[2*row] << " "; 
        }
        //cout << endl;

    }

    // Compute Ex
    temp.clear();
    // Setup input for IDCT
    double w_u, w_v, denom_inv;
    for(int row = 0; row < BINS_PER_ROW; row++) { //looping params implement transpose!
        //cout << endl << "IDCT input to AIE:" << endl << std::setprecision(2);
        for(int col = 0; col < BINS_PER_ROW; col++) {
            if(row == 0 && col == 0) 
                { w_u = 0; w_v = 0; denom_inv = 0;} // for 0, 0 we avoid division by 0
            else {
                w_u = 2*M_PI*row / BINS_PER_ROW;
                w_v = 2*M_PI*col / BINS_PER_ROW;
                denom_inv = 1 / (w_u*w_u + w_v*w_v);
            }
            input_data[2*col] = grid.getBin(row, col).a_uv * w_u * denom_inv;
            input_data[2*col+1] = 0; // imaginary part is expected for FFT input
            //cout << input_data[2*col] << " "; 
        }
        //cout << endl;


        // Send data to IDCT graph
        density_driver[1].send_packet(input_data);
        density_driver[1].receive_packet(output_data);

        //cout << endl << "IDCT output from AIE:" << endl << std::setprecision(2);
        std::vector<float> res;
        for(int col = 0; col < BINS_PER_ROW; col++) {
            res.push_back(output_data[2*col]);
            //cout << output_data[2*col] << " "; 
        }
        //cout << endl;
        temp.push_back(res);
    }

    for(int col = 0; col < BINS_PER_ROW; col++) {
        for(int row = 0; row < BINS_PER_ROW; row++) { // looping order performs IDXST on columns
            input_data[2*row] = temp[row][col];
            input_data[2*row+1] = 0;
        }

        // Send data to IDXST graph
        density_driver[2].send_packet(input_data);
        density_driver[2].receive_packet(output_data);

        //cout << endl << "IDXST output:" << endl << std::setprecision(2);
        // Store the result Ex transposed (for comparison)
        for(int row = 0; row < BINS_PER_ROW; row++) {
            grid.getBin(row, col).eField.x = output_data[2*row];
            //cout << output_data[2*row] << " "; 
        }
        //cout << endl;
    }

    // Compute Ey
    temp.clear();
    // Setup input for IDXST
    for(int row = 0; row < BINS_PER_ROW; row++) { //looping params implement transpose!
        //cout << endl << "IDXST input to AIE:" << endl << std::setprecision(2);
        for(int col = 0; col < BINS_PER_ROW; col++) {
            if(row == 0 && col == 0) 
                { w_u = 0; w_v = 0; denom_inv = 0;} // for a(0, 0) we avoid division by 0 (remove dc component)
            else {
                w_u = 2*M_PI*row / BINS_PER_ROW;
                w_v = 2*M_PI*col / BINS_PER_ROW;
                denom_inv = 1 / (w_u*w_u + w_v*w_v);
            }
            input_data[2*col] = grid.getBin(row, col).a_uv * w_v * denom_inv;
            input_data[2*col+1] = 0;
            //cout << input_data[2*col] << " "; 
        }
        //cout << endl;


        // Send data to IDXST graph
        density_driver[2].send_packet(input_data);
        density_driver[2].receive_packet(output_data);

        //cout << endl << "IDXST output from AIE:" << endl << std::setprecision(2);
        std::vector<float> res;
        for(int col = 0; col < BINS_PER_ROW; col++) {
            res.push_back(output_data[2*col]);
            //cout << output_data[2*col] << " "; 
        }
        //cout << endl;
        temp.push_back(res);
    }

    for(int col = 0; col < BINS_PER_ROW; col++) {
        for(int row = 0; row < BINS_PER_ROW; row++) { // looping order performs IDCT on columns
            input_data[2*row] = temp[row][col];
            input_data[2*row+1] = 0;
        }

        // Send data to IDCT graph
        density_driver[1].send_packet(input_data);
        density_driver[1].receive_packet(output_data);

        //cout << endl << "IDCT output:" << endl << std::setprecision(2);
        // Store the result Ex transposed (for comparison)
        for(int row = 0; row < BINS_PER_ROW; row++) {
            grid.getBin(row, col).eField.y = output_data[2*row];
            //cout << output_data[2*row] << " "; 
        }
        //cout << endl;
    }
}


/***************
 * CPU functions
****************/

// Alignment to prevent false sharing (adjust for your CPU cache line size)
#define CACHE_LINE_SIZE 64

// Aligned thread-local structure to prevent false sharing
struct alignas(CACHE_LINE_SIZE) ThreadLocalResults {
    // Using node pointer as first key to improve locality
    std::map<Node*, std::map<Net*, XY>> node_to_net_partials;
    
    // Pad to ensure no false sharing between thread data
    char padding[CACHE_LINE_SIZE - sizeof(std::map<Node*, std::map<Net*, XY>>) % CACHE_LINE_SIZE];
};

// Memory optimized computeAllPartials function
void Placer::computeAllPartials_CPU()
{
    TIME_FUNCTION();
    // Memory-bandwidth optimized implementation
    // STEP 1: Sequential pre-processing - single-threaded
    auto& nets = db.getNetsVector();
    
    // We'll use a flat data structure for results
    struct NetNodePartial {
        Net* net;
        Node* node;
        Point partial;
    };
    
    // Pre-allocate all memory needed
    // This avoids allocations during parallel processing
    size_t total_node_count = 0;
    for (auto* net : nets) {
        total_node_count += net->getDegree();
    }
    
    std::vector<NetNodePartial> all_partials;
    all_partials.reserve(total_node_count);
    
    // STEP 2: Single-threaded computation - test baseline performance
    auto start_single = std::chrono::high_resolution_clock::now();
    
    // Process all nets sequentially
    for (Net* net_p : nets) {
        const std::vector<Node*>& nodes = net_p->getNodes();
        int net_size = net_p->getDegree();
        
        // Skip further processing for very small nets
        if (net_size <= 1) continue;
        
        // Record starting index for this net's results
        size_t start_idx = all_partials.size();
        
        // Pre-allocate space for all results from this net
        all_partials.resize(start_idx + net_size);
        
        // find max and min x and y positions
        float min_x = __FLT_MAX__, min_y = __FLT_MAX__, max_x = 0, max_y = 0;
        for (Node* node_p : nodes) {
            min_x = std::min(min_x, node_p->getX());
            min_y = std::min(min_y, node_p->getY());
            max_x = std::max(max_x, node_p->getX());
            max_y = std::max(max_y, node_p->getY());
        }
        
        // Compute A terms directly into our flat vector
        std::vector<Term> A(net_size);
        for (size_t i = 0; i < net_size; i++) {
            A[i].plus.x  = exp((nodes[i]->getX() - max_x) / gamma);
            A[i].minus.x = exp((min_x - nodes[i]->getX()) / gamma);
            A[i].plus.y  = exp((nodes[i]->getY() - max_y) / gamma);
            A[i].minus.y = exp((min_y - nodes[i]->getY()) / gamma);
        }
        
        // Compute B and C terms
        Term B, C;
        B.clear(); C.clear();
        for (size_t i = 0; i < net_size; i++) {
            B.plus.x  += A[i].plus.x;
            B.minus.x += A[i].minus.x;
            B.plus.y  += A[i].plus.y;
            B.minus.y += A[i].minus.y;
            C.plus.x  += A[i].plus.x  * nodes[i]->getX();
            C.minus.x += A[i].minus.x * nodes[i]->getX();
            C.plus.y  += A[i].plus.y  * nodes[i]->getY();
            C.minus.y += A[i].minus.y * nodes[i]->getY();
        }
        
        // Pre-compute common terms
        float inv_gamma = 1.0f / gamma;
        float bpx_sq_inv = 1.0f / (B.plus.x * B.plus.x);
        float bmx_sq_inv = 1.0f / (B.minus.x * B.minus.x);
        float bpy_sq_inv = 1.0f / (B.plus.y * B.plus.y);
        float bmy_sq_inv = 1.0f / (B.minus.y * B.minus.y);
        
        // Compute partials and store in our flat vector
        for (size_t i = 0; i < net_size; i++) {
            float x = nodes[i]->getX();
            float y = nodes[i]->getY();
            
            Point partial;
            partial.x = ((1 + x * inv_gamma) * B.plus.x - (C.plus.x * inv_gamma)) 
                      * (A[i].plus.x * bpx_sq_inv)
                    - ((1 - x * inv_gamma) * B.minus.x + (C.minus.x * inv_gamma)) 
                      * (A[i].minus.x * bmx_sq_inv);
                      
            partial.y = ((1 + y * inv_gamma) * B.plus.y - (C.plus.y * inv_gamma)) 
                      * (A[i].plus.y * bpy_sq_inv)
                    - ((1 - y * inv_gamma) * B.minus.y + (C.minus.y * inv_gamma)) 
                      * (A[i].minus.y * bmy_sq_inv);
            
            // Store in our flat array
            all_partials[start_idx + i].net = net_p;
            all_partials[start_idx + i].node = nodes[i];
            all_partials[start_idx + i].partial = partial;
        }



        //for(size_t i = 0; i < net_size; i++) {
        //    net_p->mm_partials_by_node[nodes[i]].x = (( 1 + nodes[i]->getX()/gamma) * B.plus.x - (C.plus.x / gamma)) 
        //                                * (A[i].plus.x / (B.plus.x * B.plus.x))
        //                        - (( 1 - nodes[i]->getX()/gamma) * B.minus.x + (C.minus.x / gamma)) 
        //                                * (A[i].minus.x / (B.minus.x * B.minus.x));

        //    net_p->mm_partials_by_node[nodes[i]].y = (( 1 + nodes[i]->getY()/gamma) * B.plus.y - (C.plus.y / gamma)) 
        //                                * (A[i].plus.y / (B.plus.y * B.plus.y))
        //                        - (( 1 - nodes[i]->getY()/gamma) * B.minus.y + (C.minus.y / gamma)) 
        //                                * (A[i].minus.y / (B.minus.y * B.minus.y));
        //    //Logger::log_debug(nodes[i]->getName() + " partial_x: " + std::to_string(net_p->mm_partials_by_node[nodes[i]].x));
        //    //Logger::log_debug(nodes[i]->getName() + " partial_y: " + std::to_string(net_p->mm_partials_by_node[nodes[i]].y));
        //}
    }
    
    auto end_single = std::chrono::high_resolution_clock::now();
    auto duration_single = std::chrono::duration_cast<std::chrono::milliseconds>(end_single - start_single).count();
    Logger::log_detail("Sequential computation took " + std::to_string(duration_single) + " ms");
    
    // STEP 3: Update final data structures
    auto start_update = std::chrono::high_resolution_clock::now();
    
    // Simple linear update to final data structure
    for (const auto& entry : all_partials) {
        entry.net->mm_partials_by_node[entry.node] = entry.partial;
    }
    
    auto end_update = std::chrono::high_resolution_clock::now();
    auto duration_update = std::chrono::duration_cast<std::chrono::milliseconds>(end_update - start_update).count();
    Logger::log_detail("Final update took " + std::to_string(duration_update) + " ms");
    
    // Option to try parallel processing later if needed
    /*
    // Only enable parallelism if we've verified it helps
    if (total_node_count > 100000) {  // Example threshold
        const int num_threads = 4;  // Fixed thread count based on testing
        tbb::task_arena arena(num_threads);
        arena.execute([&]{
            // Parallel processing here
        });
    }
    */
}


//void Placer::computeAllPartials_CPU()
//    // This is the original computeAllPartials function
//    // It is not optimized for memory bandwidth and is not parallelized
//    // It is kept here for reference and comparison
//{
//    TIME_FUNCTION();
//    Logger::log_info("Iteration " + std::to_string(iteration) );
//    auto & nets = db.getNetsVector();
//    
//    // Pre-sort nets by size to improve work distribution
//    std::vector<std::pair<size_t, Net*>> sorted_nets;
//    sorted_nets.reserve(nets.size());
//    for (size_t i = 0; i < nets.size(); ++i) {
//        sorted_nets.emplace_back(nets[i]->getDegree(), nets[i]);
//    }
//    
//    // Sort largest nets first for better load balancing
//    std::sort(sorted_nets.begin(), sorted_nets.end(), 
//              [](const auto& a, const auto& b) { return a.first > b.first; });
//    
//    // Try a smaller number of threads first (start with logical cores, not hyperthreads)
//    // Adjust based on your hardware - this is just an example
//    //int num_threads = std::thread::hardware_concurrency() / 2;
//    int num_threads = MAX_THREADS * iteration;
//    if (num_threads < 1) num_threads = 1;
//    
//    // Allocate thread local storage
//    std::vector<ThreadLocalResults> thread_results(num_threads);
//    
//    // Create atomic counter for work stealing
//    std::atomic<size_t> next_index(0);
//    
//    // Performance measurement
//    auto start_time = std::chrono::high_resolution_clock::now();
//    
//    // Create task arena with explicit thread count
//    tbb::task_arena arena(num_threads);
//    arena.execute([&]{
//        // Work stealing approach - each thread grabs the next chunk of work
//        tbb::parallel_for(0, num_threads, [&](int thread_idx) {
//            // Get this thread's local result storage
//            auto& local_result = thread_results[thread_idx];
//            
//            // Work-stealing loop
//            size_t work_index;
//            while ((work_index = next_index.fetch_add(1)) < sorted_nets.size()) {
//                Net* net_p = sorted_nets[work_index].second;
//                
//                // Process this net
//                int net_size = net_p->getDegree();
//                const std::vector<Node*> &nodes = net_p->getNodes();
//                
//                // Pre-allocate vectors to avoid reallocation
//                std::vector<Term> A(net_size);
//                
//                // Cache min/max calculations
//                float min_x = __FLT_MAX__, min_y = __FLT_MAX__, max_x = 0, max_y = 0;
//                
//                // Local copies of frequently accessed data
//                const float gamma_val = gamma; // Cache this value
//                
//                // Stage 1: Find min/max x,y
//                for(Node* node_p : nodes) {
//                    float x = node_p->getX();
//                    float y = node_p->getY();
//                    min_x = std::min(min_x, x);
//                    min_y = std::min(min_y, y);
//                    max_x = std::max(max_x, x);
//                    max_y = std::max(max_y, y);
//                }
//                
//                // Stage 2: Compute A terms
//                for(size_t i = 0; i < net_size; i++) {
//                    float x = nodes[i]->getX();
//                    float y = nodes[i]->getY();
//                    
//                    A[i].plus.x  = exp((x - max_x) / gamma_val);
//                    A[i].minus.x = exp((min_x - x) / gamma_val);
//                    A[i].plus.y  = exp((y - max_y) / gamma_val);
//                    A[i].minus.y = exp((min_y - y) / gamma_val);
//                }
//                
//                // Stage 3: Compute B and C terms
//                Term B, C;
//                B.clear(); C.clear();
//                for(size_t i = 0; i < net_size; i++) {
//                    float x = nodes[i]->getX();
//                    float y = nodes[i]->getY();
//                    
//                    B.plus.x  += A[i].plus.x;
//                    B.minus.x += A[i].minus.x;
//                    B.plus.y  += A[i].plus.y;
//                    B.minus.y += A[i].minus.y;
//                    
//                    C.plus.x  += A[i].plus.x  * x;
//                    C.minus.x += A[i].minus.x * x;
//                    C.plus.y  += A[i].plus.y  * y;
//                    C.minus.y += A[i].minus.y * y;
//                }
//                
//                // Stage 4: Compute partials
//                // Pre-compute common terms to reduce redundant calculations
//                float inv_gamma = 1.0f / gamma_val;
//                float bpx_sq_inv = 1.0f / (B.plus.x * B.plus.x);
//                float bmx_sq_inv = 1.0f / (B.minus.x * B.minus.x);
//                float bpy_sq_inv = 1.0f / (B.plus.y * B.plus.y);
//                float bmy_sq_inv = 1.0f / (B.minus.y * B.minus.y);
//                
//                float cpx_div_gamma = C.plus.x * inv_gamma;
//                float cmx_div_gamma = C.minus.x * inv_gamma;
//                float cpy_div_gamma = C.plus.y * inv_gamma;
//                float cmy_div_gamma = C.minus.y * inv_gamma;
//                
//                for(size_t i = 0; i < net_size; i++) {
//                    Node* node = nodes[i];
//                    float x = node->getX();
//                    float y = node->getY();
//                    
//                    // More efficient calculation with pre-computed terms
//                    XY partial;
//                    partial.x = ((1 + x * inv_gamma) * B.plus.x - cpx_div_gamma) 
//                              * (A[i].plus.x * bpx_sq_inv)
//                            - ((1 - x * inv_gamma) * B.minus.x + cmx_div_gamma) 
//                              * (A[i].minus.x * bmx_sq_inv);
//                              
//                    partial.y = ((1 + y * inv_gamma) * B.plus.y - cpy_div_gamma) 
//                              * (A[i].plus.y * bpy_sq_inv)
//                            - ((1 - y * inv_gamma) * B.minus.y + cmy_div_gamma) 
//                              * (A[i].minus.y * bmy_sq_inv);
//                    
//                    // Store in thread-local result map
//                    local_result.node_to_net_partials[node][net_p] = partial;
//                }
//            }
//        });
//    });
//    
//    auto end_time = std::chrono::high_resolution_clock::now();
//    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
//    
//    Logger::log_detail("Parallel computation took " + std::to_string(duration) + " ms with " + 
//                      std::to_string(num_threads) + " threads");
//                      
//    // Merge results efficiently by organizing by node first
//    start_time = std::chrono::high_resolution_clock::now();
//    
//    for (auto& thread_result : thread_results) {
//        for (auto& [node, net_map] : thread_result.node_to_net_partials) {
//            for (auto& [net, partial] : net_map) {
//                net->mm_partials_by_node[node] = partial;
//            }
//        }
//    }
//    
//    end_time = std::chrono::high_resolution_clock::now();
//    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
//    Logger::log_detail("Merging results took " + std::to_string(duration) + " ms");
//}


/*
 * @brief On CPU, compute Electric fields using 2D-DCT method
 *
 * Results of the partial derivative computation are stored 
 * within each node's data members
**/
//const int MAX_THREADS = 5; //std::thread::hardware_concurrency(); //On NextGenIO: 96
void Placer::computeAllPartials_CPU_orig()
{
        // The simplest parallel_for usage for your derivatives
        //void computeDerivatives(std::vector<double>& results, const Design& design) {
        //    tbb::parallel_for(
        //        tbb::blocked_range<size_t>(0, results.size()),
        //        [&](const tbb::blocked_range<size_t>& r) {
        //            for(size_t i = r.begin(); i < r.end(); ++i) {
        //                results[i] = computeSingleDerivative(design, i);
        //            }
        //        }
        //    );
        //}

    TIME_FUNCTION();


// Phase 1: Compute partials for each net, stores result in net object to be inherently thread-safe
// compute only for nets size 2-8, which is what normally runs on AIE
    //Logger::log_detail("MAX_THREADS: " + std::to_string(MAX_THREADS));
    //for (auto item : db.getNetsByDegree()) 
    {
    

    // Multithread ALL nets as one big group


        //std::vector<std::thread> partials_threads;
        std::vector<Net*> processed_nets; // Debugging vector to check all nets are processed
        std::vector<std::thread::id> thread_ids;
        std::map<std::thread::id, int> thread_ids_map;
        std::mutex thread_ids_mutex;
        //if(item.first < MIN_AIE_NET_SIZE || item.first > MAX_AIE_NET_SIZE) 
        //    continue;
        //else 
        // parallel implementation
        {
            cout << "Iteration " << iteration << endl;
            //" -- Computing partials for nets of size " << item.first << endl;
            //auto & nets = item.second; // all nets of this size in the DataBase
            auto & nets = db.getNetsVector(); // all nets in the DataBase
            size_t block_range = nets.size();
            size_t grain_size  = nets.size() / MAX_THREADS;
            Logger::log_detail("nets.size(): " + std::to_string(block_range) + " grain_size: " + std::to_string(grain_size));

            // use tbb for parallelization


            tbb::task_arena arena(MAX_THREADS);
            arena.execute([&]{
            tbb::parallel_for(
                //tbb::blocked_range<size_t>(0, block_range, grain_size ), //omit grain_size allows tbb to decide
                tbb::blocked_range<size_t>(0, block_range), //omit grain_size allows tbb to decide
                [&](const tbb::blocked_range<size_t>& r) {
                    //std::lock_guard<std::mutex> lock(thread_ids_mutex);
                    //thread_ids_mutex.lock();
                    //Logger::log_detail("BEGIN Thread ID: " + std::to_string(get_index(std::this_thread::get_id())) + " -- block_range: " + std::to_string(r.end() - r.begin()) );
                    //thread_ids_mutex.unlock();
                    //cout << endl;
                    for(size_t i = r.begin(); i < r.end(); ++i) {
                        //thread_ids_mutex.lock();
                        //thread_ids.push_back(std::this_thread::get_id());
                        //processed_nets.push_back(nets[i]); // Debugging
                        //thread_ids_map[std::this_thread::get_id()]++;
                        //thread_ids_mutex.unlock();

                        computeNetPartials_ThreadSafe(nets[i]);
                        //computeNetPartials_CPU(nets[i]);
                    }
                    //thread_ids_mutex.lock();
                    //Logger::log_detail("END Thread ID: " + std::to_string(get_index(std::this_thread::get_id())));                    
                    //thread_ids_mutex.unlock();
                }
            );
            });
            //std::cout << "Threads used: " << thread_ids.size() << std::endl;

        }

        // DEBUG: print all nets processed
        //for(int i = 0; i < processed_nets.size(); i++) {
        //    cout << thread_ids[i] << " -- " << thread_ids_map[thread_ids[i]] <<  " -- Net " << i << ": " << processed_nets[i]->getName() << endl;
        //}   
        //cout << "function count: " << processed_nets.size() << endl;

        // sequential implementation
        //for (Net* net_p : item.second) {
            //launch a batch of threads
            //partials_threads.emplace_back(&AIEplace::Placer::computeNetPartials_CPU, this, net_p);
        //    computeNetPartials_CPU(net_p); // no multithreading
            //if(partials_threads.size() %1000 == 0 || partials_threads.size() > 4000)
            //Logger::log_detail("Nets of size " + std::to_string(item.first) + ": " + std::to_string(item.second.size()) + " -- Thread count: " + std::to_string(partials_threads.size()));
            //Logger::log_detail("New thread launched (ID == " + std::to_string(get_index(partials_threads.back().get_id()))+ ") for net " + net_p->getName() + " -- Thread count: " + std::to_string(partials_threads.size()));

            // Join threads
            //if(partials_threads.size() >= MAX_THREADS) {
            //    for(auto& thread : partials_threads) {
            //        if(thread.joinable()) {
            //            //Logger::log_detail("Join thread... Thread count: " + std::to_string(partials_threads.size()));
            //            thread.join();
            //        }
            //    }
            //    partials_threads.clear();
            //}
        //}

            //Ensure all remaining threads are joined
            //for(auto& thread : partials_threads) {
            //    if(thread.joinable()) {
            //        //Logger::log_detail("Join thread... Thread count: " + std::to_string(partials_threads.size()));
            //        thread.join();
            //    }
            //}
    }


    // Phase 2: add up the partials
    for (auto net : db.getNets()) {
        for(auto pair : net.second->mm_partials_by_node) {
            Node* node = pair.first;
            XY partials = pair.second;
            //node->printXY();
            //Logger::log_detail("Node: " + node->getName() + " -- partials: " + std::to_string(partials.x) + ", " + std::to_string(partials.y));
        }
    }

    // DEBUG: Print partials results
    //
    //for ( auto item :  db.getComponents() )
    //{
    //    Node* node_p = item.second;
    //    cout << node_p->getName() << "\tsum_of_partials_x: " << node_p->terms_cpu.partials.x << endl;
    //    cout << node_p->getName() << "\tsum_of_partials_y: " << node_p->terms_cpu.partials.y << endl;

    //    for ( Net* net_p : node_p->getNets())
    //        cout << net_p->to_string();
    //    cout << "####################" << endl;

    //}
}

void Placer::compute_a_terms_CPU(Net* net_p)
{
    // X positions
    net_p->sortPositionsByX();
    std::vector<Node*> nodes = net_p->getNodes();
    for (Node* node_p : nodes) {
        node_p->terms_cpu.a.plus.x  = exp( (node_p->getX() - nodes.front()->getX()) / gamma);
        node_p->terms_cpu.a.minus.x = exp( (nodes.back()->getX() - node_p->getX()) / gamma);
        //if(node_p->terms_cpu.a.plus.x > 1)
        //Logger::log_detail("CPU computed a+ = " + std::to_string(node_p->terms_cpu.a.plus.x));
        assert(node_p->terms_cpu.a.plus.x <= 1 && "Invalid a+ computed!");
        //if(node_p->terms_cpu.a.minus.x > 1)
        //Logger::log_detail("CPU computed a- = " + std::to_string(node_p->terms_cpu.a.minus.x));
        assert(node_p->terms_cpu.a.minus.x <= 1 && "Invalid a- computed!");
    }

    // Y positions
    net_p->sortPositionsByY();
    nodes = net_p->getNodes();
    for (Node* node_p : nodes) {
        node_p->terms_cpu.a.plus.y  = exp( (node_p->getY() - nodes.front()->getY()) / gamma);
        node_p->terms_cpu.a.minus.y = exp( (nodes.back()->getY() - node_p->getY()) / gamma);
    }
}

void Placer::compute_bc_terms_CPU(Net* net_p)
{
    compute_a_terms_CPU(net_p);
    for (Node* node_p : net_p->getNodes()) {
        // compute b terms
        net_p->terms_cpu.b.plus.x  += node_p->terms_cpu.a.plus.x;
        net_p->terms_cpu.b.minus.x += node_p->terms_cpu.a.minus.x;
        net_p->terms_cpu.b.plus.y  += node_p->terms_cpu.a.plus.y;
        net_p->terms_cpu.b.minus.y += node_p->terms_cpu.a.minus.y;

        // compute c terms
        net_p->terms_cpu.c.plus.x  += node_p->terms_cpu.a.plus.x  * node_p->getX();
        net_p->terms_cpu.c.minus.x += node_p->terms_cpu.a.minus.x * node_p->getX();
        net_p->terms_cpu.c.plus.y  += node_p->terms_cpu.a.plus.y  * node_p->getY();
        net_p->terms_cpu.c.minus.y += node_p->terms_cpu.a.minus.y * node_p->getY();
    }
}

/* @brief: For each node on net_p, compute partial derivative with respect to the net.
 *         Add result to the node's partials term
 */
void Placer::computeNetPartials_CPU(Net* net_p)
{
    try {
    //net_p->lockNodes();
    // DEBUG: stop at max net size for comparison to AIE computation
    assert(net_p->getDegree() >= MIN_AIE_NET_SIZE);
    assert(net_p->getDegree() <= MAX_AIE_NET_SIZE);

    compute_bc_terms_CPU(net_p);
    for (Node* node_p : net_p->mv_nodes) {
        float partial_x = (( 1 + node_p->getX()/gamma) * net_p->terms_cpu.b.plus.x - (net_p->terms_cpu.c.plus.x / gamma)) 
                                    * (node_p->terms_cpu.a.plus.x / (net_p->terms_cpu.b.plus.x * net_p->terms_cpu.b.plus.x))
                         - (( 1 - node_p->getX()/gamma) * net_p->terms_cpu.b.minus.x + (net_p->terms_cpu.c.minus.x / gamma)) 
                                    * (node_p->terms_cpu.a.minus.x / (net_p->terms_cpu.b.minus.x * net_p->terms_cpu.b.minus.x));

        float partial_y = (( 1 + node_p->getY()/gamma) * net_p->terms_cpu.b.plus.y - (net_p->terms_cpu.c.plus.y / gamma)) 
                                    * (node_p->terms_cpu.a.plus.y / (net_p->terms_cpu.b.plus.y * net_p->terms_cpu.b.plus.y))
                         - (( 1 - node_p->getY()/gamma) * net_p->terms_cpu.b.minus.y + (net_p->terms_cpu.c.minus.y / gamma)) 
                                    * (node_p->terms_cpu.a.minus.y / (net_p->terms_cpu.b.minus.y * net_p->terms_cpu.b.minus.y));

        Logger::log_debug(node_p->getName() + " partial_x: " + std::to_string(partial_x));
        Logger::log_debug(node_p->getName() + " partial_y: " + std::to_string(partial_y));
        node_p->terms_cpu.partials.x += partial_x;
        node_p->terms_cpu.partials.y += partial_y;
    }
    for (Node* node_p : net_p->mv_nodes) {
        Logger::log_debug(node_p->getName() + " total partial_x: " + std::to_string(node_p->terms_cpu.partials.x));
        Logger::log_debug(node_p->getName() + " total partial_y: " + std::to_string(node_p->terms_cpu.partials.y));
    }
    net_p->unlockNodes();
    } catch (std::exception& e) {
        Logger::log_critical("Exception in computeNetPartials_CPU: " + std::string(e.what()));
    } catch (...) {
        Logger::log_critical("Unknown exception in computeNetPartials_CPU");
    }
}


//static int timeout[8];

/* @brief: For each node on net_p, compute partial derivative with respect to the net.
 *         Function written to be inherently thread-safe without the need for mutexes
 *         Results are written to the partials map 
 */
void Placer::computeNetPartials_ThreadSafe(Net* net_p)
{
    //net_p->tally++;
    //if(net_p->mv_nodes.size() == 2) return;
    //if(net_p->mv_nodes.size() > 3)
    //if(timeout[net_p->getDegree()]++ > 100) {
    //    Logger::log_critical("Timeout in computeNetPartials_ThreadSafe");
    //    return;
    //}

    int net_size = net_p->getDegree();
    const std::vector<Node*> &nodes = net_p->getNodes();

    // find max and min x and y positions
    float min_x = __FLT_MAX__, min_y = __FLT_MAX__, max_x = 0, max_y = 0;
    for(Node* node_p : net_p->getNodes()) {
        if(node_p->getX() < min_x) min_x = node_p->getX();
        if(node_p->getY() < min_y) min_y = node_p->getY();
        if(node_p->getX() > max_x) max_x = node_p->getX();
        if(node_p->getY() > max_y) max_y = node_p->getY();
    }

    // compute A terms for each node in the net
    //Logger::log_detail("\n######\nNet: " + net_p->getName() + " -- min_x: " + std::to_string(min_x) + " max_x: " + std::to_string(max_x));
    vector<Term> A(net_size);
    for(size_t i = 0; i < net_size; i++) {
        A[i].plus.x  = exp( (nodes[i]->getX() - max_x) / gamma);
        A[i].minus.x = exp( (min_x - nodes[i]->getX()) / gamma);
        A[i].plus.y  = exp( (nodes[i]->getY() - max_y) / gamma);
        A[i].minus.y = exp( (min_y - nodes[i]->getY()) / gamma);

        //nodes[i]->printXY();
        //Logger::log_detail(" A+ x: " + std::to_string(A[i].plus.x));
        //Logger::log_detail(" A- x: " + std::to_string(A[i].minus.x));
        //Logger::log_detail(" A+ y: " + std::to_string(A[i].plus.y));
        //Logger::log_detail(" A- y: " + std::to_string(A[i].minus.y));
    }

    // compute B and C terms for this net
    Term B, C;
    B.clear(); C.clear();
    for(size_t i = 0; i < net_size; i++) {
        B.plus.x  += A[i].plus.x;
        B.minus.x += A[i].minus.x;
        B.plus.y  += A[i].plus.y;
        B.minus.y += A[i].minus.y;

        C.plus.x  += A[i].plus.x  * nodes[i]->getX();
        C.minus.x += A[i].minus.x * nodes[i]->getX();
        C.plus.y  += A[i].plus.y  * nodes[i]->getY();
        C.minus.y += A[i].minus.y * nodes[i]->getY();
    }

    //Logger::log_detail("B+ x: " + std::to_string(B.plus.x));
    //Logger::log_detail("B- x: " + std::to_string(B.minus.x));
    //Logger::log_detail("B+ y: " + std::to_string(B.plus.y));
    //Logger::log_detail("B- y: " + std::to_string(B.minus.y));

    //Logger::log_detail("C+ x: " + std::to_string(C.plus.x));
    //Logger::log_detail("C- x: " + std::to_string(C.minus.x));
    //Logger::log_detail("C+ y: " + std::to_string(C.plus.y));
    //Logger::log_detail("C- y: " + std::to_string(C.minus.y));

    // compute partials, store result in Net object
    for(size_t i = 0; i < net_size; i++) {
        net_p->mm_partials_by_node[nodes[i]].x = (( 1 + nodes[i]->getX()/gamma) * B.plus.x - (C.plus.x / gamma)) 
                                    * (A[i].plus.x / (B.plus.x * B.plus.x))
                             - (( 1 - nodes[i]->getX()/gamma) * B.minus.x + (C.minus.x / gamma)) 
                                    * (A[i].minus.x / (B.minus.x * B.minus.x));

        net_p->mm_partials_by_node[nodes[i]].y = (( 1 + nodes[i]->getY()/gamma) * B.plus.y - (C.plus.y / gamma)) 
                                    * (A[i].plus.y / (B.plus.y * B.plus.y))
                             - (( 1 - nodes[i]->getY()/gamma) * B.minus.y + (C.minus.y / gamma)) 
                                    * (A[i].minus.y / (B.minus.y * B.minus.y));
        //Logger::log_debug(nodes[i]->getName() + " partial_x: " + std::to_string(net_p->mm_partials_by_node[nodes[i]].x));
        //Logger::log_debug(nodes[i]->getName() + " partial_y: " + std::to_string(net_p->mm_partials_by_node[nodes[i]].y));
    }
    

    // partials will be accumulated with other nodes elsewhere

}

/*
 * @brief On CPU, compute Electric fields using 2D-DCT method
 *
**/
// wrong result?
void Placer::computeElectricFields_CPU()
{
    Logger::log_detail("Begin computeElectricFields_CPU()");
    compute_a_uv_naive();
    compute_eField_naive();
}

void Placer::computeElectricFields_DCT()
{
    Logger::log_detail("Begin computeElectricFields_DCT()");
    compute_a_uv_DCT();
    compute_eField_DCT();
}

void Placer::normalizeElectricFields()
{
    float max_abs = 0;
    // find the max absolute value of all eFields
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            if(abs(grid.getBin(x, y).eField.x) > max_abs)
                max_abs = abs(grid.getBin(x, y).eField.x);
            if(abs(grid.getBin(x, y).eField.y) > max_abs)
                max_abs = abs(grid.getBin(x, y).eField.y);
        }
    }

    // normalize values
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            grid.getBin(x, y).eField.x = grid.getBin(x, y).eField.x / max_abs;
            grid.getBin(x, y).eField.y = grid.getBin(x, y).eField.y / max_abs;
        }
    }

}

// Compute the intermediate term a_uv for Efields
// Store results in each bin
// Implements DREAMplace Eq 3a
void Placer::compute_a_uv_naive()
{
    std::vector< std::vector<float> > rho = grid.getRho();
    for (int u = 0; u < grid.getBinsPerRow(); u++) {
// cout << "Computing intermediate a at u = " << u << endl;
        for (int v = 0; v < grid.getBinsPerCol(); v++) {
// cout << "v = " << v << endl;
            //float w_u = 1 * M_PI * u / grid.getBinsPerRow();
            //float w_v = 1 * M_PI * v / grid.getBinsPerCol();
            float w_u = 2 * M_PI * u / grid.getBinsPerRow();
            float w_v = 2 * M_PI * v / grid.getBinsPerCol();

            // For each bin at (u, v) compute the intermediate term a
            float a_uv = 0;
            for (int x = 0; x < grid.getBinsPerRow(); x++) {
                for (int y = 0; y < grid.getBinsPerCol(); y++) {
                    a_uv += rho[x][y] * cos(w_u*x) * cos(w_v*y); // is this in radians? or degrees?
                    //cout << "rho: " << rho[x][y] << "\toverlap/bb.area: " << (grid.getBin(x, y).overlap / grid.getBin(x, y).bb.getArea()) << endl;
                    //a_uv += (grid.getBin(x, y).overlap / grid.getBin(x, y).bb.getArea()) * cos(w_u*x) * cos(w_v*y);
                }
            }
            //a_uv /= 2 * grid.getBinsPerRow(); // 1 / 2n
            a_uv /= grid.getBinsPerCol() * grid.getBinsPerRow(); // 1 / M^2
            grid.getBin(u, v).a_uv = a_uv;
        }
    }
}

// Compute the x and y components of electric fields
// Implements DREAMplace Eq 3c, 3d
void Placer::compute_eField_naive()
{
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
//cout << "Computing eField at x = " << x << endl;
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
//cout << "y = " << y << endl;
            float w = 2 * M_PI / grid.getBinsPerRow();
            // For each bin at (u, v) compute the intermediate term a
            float Ex = 0;
            float Ey = 0;
            for (int u = 0; u < grid.getBinsPerRow(); u++) {
                for (int v = 0; v < grid.getBinsPerCol(); v++) {
                    if ( u == 0 && v == 0) continue; // avoid division by 0
                    float w_u = w*u;
                    float w_v = w*v;
                    float a_uv = grid.getBin(u, v).a_uv;
                    Ex += (a_uv*w_u) / (w_u*w_u + w_v*w_v) * sin(w_u*x) * cos(w_v*y);
                    Ey += (a_uv*w_v) / (w_u*w_u + w_v*w_v) * cos(w_u*x) * sin(w_v*y);
                }
            }
            grid.getBin(x, y).eField.x = Ex;
            grid.getBin(x, y).eField.y = Ey;
            //cout << "Ex: " << Ex << "\tEy:" << Ey << endl;
        }
    }
}

/* @brief: Compute the intermediate term a_uv using DCTs*/
void Placer::compute_a_uv_DCT()
{
    std::vector< std::vector<float> > rho = grid.getRho();
    std::vector< std::vector<float> > temp;
    std::vector< std::vector<float> > a_uv;

    // Perform 1-D DCT on rows
    for (int row_index = 0; row_index < grid.getBinsPerCol(); row_index++)
        temp.push_back(DCT_naive(rho[row_index]));

    // DEBUG: Print 1D DCT input and result
    //cout << endl << "CPU rho input to 1D-DCT:" << endl;
    //for (int j = 0; j < grid.getBinsPerRow(); j++) 
    //for (int i = 0; i < grid.getBinsPerRow(); i++) {
    //    cout << rho[j][i] << "\t";
    //    if((i+1)%16 == 0) cout << endl;
    //}
    //cout << endl;
    //cout << endl << "CPU compute 1D-DCT output:" << endl;
    //for (int j = 0; j < grid.getBinsPerRow(); j++) 
    //for (int i = 0; i < grid.getBinsPerRow(); i++) {
    //    cout << temp[j][i] << "\t";
    //    if((i+1)%16 == 0) cout << endl;
    //}
    //cout << endl;

    temp = transpose(temp);

    // Perform 1-D DCT on transposed matrix
    for (int col_index = 0; col_index < grid.getBinsPerRow(); col_index++)
        a_uv.push_back(DCT_naive(temp[col_index]));
    
    a_uv = transpose(a_uv);

    //cout << endl << "CPU compute a_uv output:" << endl;
    //for (int j = 0; j < grid.getBinsPerRow(); j++) 
    //for (int i = 0; i < grid.getBinsPerRow(); i++) {
    //    cout << a_uv[j][i] << "\t";
    //    if((i+1)%16 == 0) cout << endl;
    //}
    //cout << endl;

    // compare to computed result from other methods
    //bool mismatch = false;
    //Table mismatches;
    //mismatches.add_row({"u", "v", "DCT result", "AIE result", "isClose"});
    //for (int u = 0; u < grid.getBinsPerRow(); u++)
    //    for (int v = 0; v < grid.getBinsPerCol(); v++) {
    //        bool close = isClose(a_uv[u][v], grid.getBin(u, v).a_uv);
    //        if(!close)
    //        {
    //            mismatch = true;
    //            mismatches.add_row(RowStream{} << std::setprecision(2) << u << v << a_uv[u][v] << grid.getBin(u, v).a_uv << close);
    //        }
    //    }
    //if(mismatch) {
    //    Table top;
    //    top.add_row(RowStream{} << "a_uv mismatch");
    //    top.add_row({mismatches});
    //    Logger::log_error(top);
    //} else Logger::log_info("Density DCT computation: all a_uv terms match!");

    for (int u = 0; u < grid.getBinsPerRow(); u++)
        for (int v = 0; v < grid.getBinsPerCol(); v++) {
           grid.getBin(u, v).a_uv = a_uv[u][v];
        }
}

/* @brief: Compute the eField values using DCTs*/
void Placer::compute_eField_DCT()
{
    int num_rows = grid.getBinsPerCol();
    int num_cols = grid.getBinsPerRow();
    std::vector< std::vector<float> > Ex     (num_rows, std::vector<float>(num_cols));
    std::vector< std::vector<float> > Ey     (num_rows, std::vector<float>(num_cols));
    std::vector< std::vector<float> > a_uv = grid.get_a_uv();

    float w = 2 * M_PI / num_cols;
    Ex[0][0] = 0; Ey[0][0] = 0;

    for (int u = 0; u < num_rows; u++) {
        for (int v = 0; v < num_cols; v++) {
            if ( u == 0 && v == 0) continue; // avoid division by 0
            float w_u = 2*M_PI*u / num_cols;
            float w_v = 2*M_PI*v / num_rows;
            float denom = w_u*w_u + w_v*w_v;

            Ex[u][v] = a_uv[u][v] * w_u / denom;
            Ey[u][v] = a_uv[u][v] * w_v / denom;
        }
    }

    // compute IDCT on all rows of Ex, and IDXST on all rows of Ey
    for (int row_index = 0; row_index < num_rows; row_index++) {
        //cout << endl << "IDCT input from CPU:" << endl << std::setprecision(2);
        //for (int col_index = 0; col_index < num_rows; col_index++)
            //cout << Ex[row_index][col_index] << " ";
        Ex[row_index] = IDCT_naive (Ex[row_index]);
        //cout << endl << "IDCT output from CPU:" << endl << std::setprecision(2);
        //for (int col_index = 0; col_index < num_rows; col_index++)
            //cout << Ex[row_index][col_index] << " ";
        Ey[row_index] = IDXST_naive(Ey[row_index]);
    }
    //cout << endl;

    Ex = transpose(Ex);
    Ey = transpose(Ey);

    // compute IDCT on all rows of Ey, and IDXST on all rows of Ex
    for (int row_index = 0; row_index < num_rows; row_index++) {
        Ex[row_index] = IDXST_naive (Ex[row_index]);
        Ey[row_index] = IDCT_naive(Ey[row_index]);
    }

    Ex = transpose(Ex);
    Ey = transpose(Ey);

    // compare to computed result from other methods

    //bool mismatch = false;
    //Table mismatches;
    //mismatches.add_row({"u", "v", "DCT result", "CPU result", "isClose"});
    //for (int u = 0; u < grid.getBinsPerRow(); u++)
    //    for (int v = 0; v < grid.getBinsPerCol(); v++) {
    //        //grid.getBin(u, v).a_uv = a_uv[u][v];
    //        bool close = isClose(Ex[u][v], grid.getBin(u, v).eField.x);
    //        if(!close)
    //        {
    //            mismatch = true;
    //            mismatches.add_row(RowStream{} <<std::setprecision(2) <<  u << v << Ex[u][v] << grid.getBin(u, v).eField.x << close);
    //        }
    //    }
    //if(mismatch) {
    //    Table top;
    //    top.add_row(RowStream{} << "eField.x mismatch");
    //    top.add_row({mismatches});
    //    Logger::log_error(top);
    //} else Logger::log_info("Density DCT computation: all eField.x terms match!");

    //Table mismatch_y;
    //mismatch = false;
    //mismatch_y.add_row({"u", "v", "DCT result", "CPU result", "isClose"});
    //for (int u = 0; u < grid.getBinsPerRow(); u++)
    //    for (int v = 0; v < grid.getBinsPerCol(); v++) {
    //        //grid.getBin(u, v).a_uv = a_uv[u][v];
    //            bool close = isClose(Ey[u][v], grid.getBin(u, v).eField.y);
    //        if(!close)
    //        {
    //            mismatch = true;
    //            mismatch_y.add_row(RowStream{} <<std::setprecision(2) <<  u << v << Ey[u][v] << grid.getBin(u, v).eField.y << close);
    //        }
    //    }
    //if(mismatch) {
    //    Table top;
    //    top.add_row(RowStream{} << "eField.y mismatch");
    //    top.add_row({mismatch_y});
    //    Logger::log_error(top);
    //} else Logger::log_info("Density DCT computation: all eField.y terms match!");
    //std::stringstream stream_x;
    //std::stringstream stream_y;
    //stream_x << "eField.x" << endl;
    //stream_y << "eField.y" << endl;
    //for (int y = 0; y < num_rows; y++) {
    //    stream_x << std::setprecision(2) << std::fixed;
    //    stream_y << std::setprecision(2) << std::fixed;
    //    for (int x = 0; x < num_cols; x++) {
    //        stream_x << Ex[x][y] << "  ";
    //        stream_y << Ey[x][y] << "  ";
    //    }
    //    stream_x << endl;
    //    stream_y << endl;
    //}

    //cout << stream_x.str();
    //cout << stream_y.str();
    
    // Put results in the grid bins
    for (int x = 0; x < num_cols; x++) {
        for (int y = 0; y < num_rows; y++) {
            grid.getBin(x, y).eField.x = Ex[x][y];
            grid.getBin(x, y).eField.y = Ey[x][y];
        }
    }
}


void Placer::computeOverlaps()
{
    Logger::log_trace("Begin computeOverlaps()");

    for (auto item : db.getComponents())
        grid.computeBinOverlaps(item.second);
    //for (auto item : db.getPins())
    //    grid.computeBinOverlaps(item.second);



    // DEBUG
    double total_node_area = 0;
    for (auto item : db.getComponents())
        total_node_area += item.second->getArea();
    //for (auto item : db.getPins())
    //    total_node_area += item.second->getArea();
    double total_overlap = 0;
    for (int col = 0; col < grid.getBinsPerRow(); col++) {
        for (int row = 0; row < grid.getBinsPerCol(); row++) {
            total_overlap += grid.getBin(col, row).overlap;
        }
    }

    Table t;
    t.add_row(RowStream{} << "total_node_area" << total_node_area<< ""<<"");
    t.add_row(RowStream{} << "total_overlap" << total_overlap);
    t.add_row(RowStream{} << "single bin area" << grid.getBin(0,0).bb.getArea() << grid.getBin(7,8).bb.getArea() );
    Logger::log("overlap", t);
}

/* To confirm that the AIE has performed a correct computation, this function
 * compares the results to the CPU computation result
 */
void Placer::comparePartialResults()
{
    int print_count = 0;
    Logger::log_info("#############################");
    Logger::log_info("Comparing Partial Results (Iteration " + std::to_string(iteration) + ")");
    auto nodes_map = db.getComponents();
    long error_count = 0, total = 0;
    for (auto const& item: nodes_map) 
    {
        Table top;
        Node* np = item.second;
        total++;
        if(np->terms_cpu.partials.isClose(np->partials_aie))
        {
            //Logger::log_data("Terms DO match for node " + np->getName()
            //        + " -- CPU result: " + np->terms_cpu.partials.toString()
            //        + " -- AIE result: " + np->partials_aie.toString());
            continue;
        }
        else 
        {
            error_count++;
            Logger::log_error("Terms DO NOT match for node " + np->getName()
                    + " -- CPU result: " + np->terms_cpu.partials.toString()
                    + " -- AIE result: " + np->partials_aie.toString());

            //cout << "error node " << np->getName() << ": " << endl; 
            //for(auto const& net_p : np->getNets())
            //{
            //    cout << "\tOn net " << net_p->getName() << ": ";
            //    for(auto const& shares_net : net_p->getNodes())
            //    {
            //        cout << shares_net->getName()<< shares_net->getPosition().to_string() << ", ";
            //    }
            //    cout << endl;
            //}
            //cout << endl;

            //Table t;
            //t.add_row({RowStream{} << " Partials" << "X" << "Y"});
            //t.add_row({RowStream{} << "CPU result" << np->terms_cpu.partials.x << np->terms_cpu.partials.y});
            //t.add_row({RowStream{} << "AIE result" << np->partials_aie.x << np->partials_aie.y});

            //top.add_row({"Node " + np->getName()});
            //top.add_row({t});
            //Logger::log_data(top);
            //if(print_count++ > 50) return;
        }
    }
    
    std::stringstream ss;
    ss << "errors: " << error_count << "\ttotal: " << total << "\tproportion: " << float(error_count)/float(total) << endl;
    Logger::log_error(ss.str());
}

void Placer::compareDensityResults()
{
}

void Placer::nudgeAllNodes()
{
    Logger::log_detail("Begin nudgeAllNodes()");
    for (auto item : db.getComponents())
        nudgeNode(item.second);
    // Assume primary IO Pins are set in place and should not be moved!
    //for (auto item : db.getPins())
    //    nudgeNode(item.second);

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


    XY move;
    // coeff is the learning rate scaled by the size of the die
    // learning rate should be dynamic for each node?
    float die_size = min( grid.getDieWidth(), grid.getDieHeight() );
    float coeff = learning_rate * die_size;
    float partials_x, partials_y; 
    if(params["use_aie_partials"]) {
        partials_x = node_p->partials_aie.x;
        partials_y = node_p->partials_aie.y;
    } else {
        partials_x = node_p->terms_cpu.partials.x;
        partials_y = node_p->terms_cpu.partials.y;
    }
    move.x = coeff * (electro_force.x - partials_x ); // we subtract the partials to reduce net size!
    move.y = coeff * (electro_force.y - partials_y );

    //cout << "learning_rate: " << learning_rate << "\tcoeff: " <<coeff<< endl;
    //cout << "electro_force.x: " << electro_force.x <<"\telectro_force.y: " << electro_force.y << endl;
    //if(move.x != move.x) // check if nan
    //{
    //    cout << endl << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << endl;
    //    cout << "move.x: " << move.x << endl;
    //    node_p->printXY();
    //    //cout << "coeff: " << coeff << endl;
    //    //cout << "electro_force: " << electro_force.x << " : " << electro_force.y << endl;
    //    cout << "partials_aie: " << node_p->partials_aie.x << " : " << node_p->partials_aie.y << endl;
    //    cout << "partials_cpu: " << node_p->terms_cpu.partials.x << " : " << node_p->terms_cpu.partials.y << endl;
    //}

    // Update the position of this node
    node_p->translate(move.x, move.y);

    // DEBUGGING
    //cout << "NudgeNode(): "<< node_p->getName() 
    //    << " grad(" << wirelen_gradient.x << ", " << wirelen_gradient.y << ")"
    //    << "\telectro(" << electro_force.x << ", " << electro_force.y << ")" << endl;
}

void Placer::printIterationResults()
{
    if (iteration % 10 == 0)
    {
        Table top;
        top.add_row(RowStream{} << "Iteration" << iteration);
        top.add_row(RowStream{} << "HPWL" << db.computeTotalWirelength(params["wirelength_method"]));
        top.add_row(RowStream{} << "Overflow" << grid.computeTotalOverflow());
        top.add_row(RowStream{} << "Learning Rate" << learning_rate);
        top.add_row(RowStream{} << "Global Lambda" << global_lambda);
        top.column(0).format().font_align(FontAlign::right);
        top.column(1).format().font_align(FontAlign::left);
        Logger::log_data(top);
    }

    // every 10 iterations, export a table in markdown
    if (iteration % 10 == 0)
        ;

    // every 10 iterations, export an image
    #ifdef CREATE_VISUALIZATION
        if(params["visualize"])
        if (iteration % int(params["iterations_per_export"]) == 0) {
            viz.drawPlacement(db, output_dir / "placement", iteration);
            viz.drawElectricField(grid, output_dir / "efield", iteration);
        }
    #endif

    Logger::export_intermediate_results(grid, output_dir, iteration);
}

void Placer::computeStatistics()
{
}

// Timing and print functions
long Placer::getTime() {
  struct timeval tm;
  gettimeofday(&tm, NULL);
  return (tm.tv_sec * 1000000)+tm.tv_usec;
}

double Placer::getInterval(long start_time, long end_time) {
  return (end_time - start_time) / 1.0e6;
}

void Placer::printFinalResults()
{
    Logger::log_info("AIEplace algorithm complete.");
    Table statistics;
    statistics.add_row({"AIEplace Run Statistics"});

    float final_hpwl = db.computeTotalWirelength(params["wirelength_method"]);
    Table results;
    results.add_row({"Benchmark name", db.getBenchmarkName()});
    results.add_row(RowStream{} << "Iterations" << iteration);
    results.add_row(RowStream{} << "CPU runtime" << "####"/*CPU_runtime*/);
    results.add_row(RowStream{} << "AIE runtime" << "####"/*AIE_runtime*/);
    results.add_row(RowStream{} << "Final HPWL" << final_hpwl);
    results.add_row(RowStream{} << "Final Overflow" << grid.computeTotalOverflow());
    results.column(0).format().font_align(FontAlign::right);
    results.column(1).format().font_align(FontAlign::left);

    Table hyperparams;
    hyperparams.add_row(RowStream{} << "gamma" << gamma);
    hyperparams.add_row(RowStream{} << "learning rate" << learning_rate );
    hyperparams.column(0).format().font_align(FontAlign::right);
    hyperparams.column(1).format().font_align(FontAlign::left);

    statistics.format().font_align(FontAlign::center);
    statistics.add_row({results});
    statistics.add_row({hyperparams});
    Logger::log_data(statistics);

    Logger::export_markdown(statistics, output_dir);
    Table function_stats = Logger::printFunctionStats();
    Logger::export_markdown(function_stats, output_dir, "function_statistics");

    Logger::ProgramStatBlock stats;
    stats.design_name = db.getBenchmarkName();
    stats.iteration_count = iteration;
    stats.final_hpwl = final_hpwl;
    stats.final_learning_rate = learning_rate;
    // timing stats
    stats.prgm_runtime = getTiming(getEpoch(), pgrm_start_time);
    stats.db_IO_time = db_IO_time;
    stats.algo_time = algo_time;
    stats.AIE_time = AIE_time;

    Logger::append_csv(stats);

    // generate image of final placement
    #ifdef CREATE_VISUALIZATION
        if(params["visualize"])
            viz.drawPlacement(db, output_dir, iteration);
    #endif

    // write placed design to DEF
    db.writeDEF(output_dir);
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
    for(int i = 0; i < params["rand_focus_nets"]; i++) {
        // pick a random net to focus which has a pin
        //std::advance(iter, rand() % nets.size());
        while(!iter->second->hasPin())
            std::advance(iter, 1);
        db.addFocusNet(iter->second);
        std::advance(iter, 1);
    }

    auto nodes = db.getComponents();
    for(int i = 0; i < params["rand_focus_nodes"]; i++) {
        // pick a random node to focus that isn't a primary IO pin
        auto node_iter = nodes.begin();
        std::advance(node_iter, rand() % nodes.size());
        db.addFocusNode(node_iter->second);
    }
}


AIEPLACE_NAMESPACE_END
