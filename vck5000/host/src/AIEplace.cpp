#include "AIEplace.h"
#include "DCT.h"
#include <cmath>

AIEPLACE_NAMESPACE_BEGIN

/* @brief: perform an entire iteration of the ePlace algorithm.
*/
void Placer::performIteration()
{
    iterationReset();

    // launch threads from this function?

    // Compute terms for HPWL partials
    computeAllPartials_AIE();
    //computeAllPartials_CPU();
    //comparePartialResults();

    // Compute Electric Fields in each bin
    computeOverlaps(); // Density Map computation
    //db.printOverlaps();
    //grid.printOverflows();

    //computeElectricFields_AIE(); // Accelerated compute on AIEs
    //computeElectricFields_CPU(); // Compute E-fields using naive algorithm 
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
    // Set the center point of die area as initial placement target
    Position<position_type> target =
                Position<position_type>(grid.getDieWidth()/2, grid.getDieHeight()/2);

    initializePlacement(target, 0, grid.getDieWidth()/4); // even spread around center
    //initializePlacement(target, 0, 500); // Close placement for testing purposes
    //return;

    bool converged = false;
    while( !converged )
    {

        // Update hyperparameters for new iteration
        // every 100 iterations, slow learning rate
        if(iteration % 100 == 0)
            learning_rate *= 0.8;

        // every 10 iterations, bump up lambda (density weighting)
        if(iteration % 10 == 0)
            global_lambda *= 1.01;

        performIteration();
        
        // check for convergence
        if (iteration == 100) // should make MAX_ITERATIONS a global var read in by run args
            converged = true;
        else iteration++;
    }
    printFinalResults();
}

/* @brief: Reset all nodes and nets in preparation for the next iteration.
*/
void Placer::iterationReset()
{
    grid.iterationReset();
    db.iterationReset();
}

// Constructor
Placer::Placer(fs::path input_dir, std::string xclbin_file) : 
        db(DataBase(input_dir)), // TODO: Database initialization should be multithreaded?
        #ifdef CREATE_VISUALIZATION
        viz(Visualizer(db.getDieArea())),
        #endif
        grid(Grid(db.getDieArea(), BINS_PER_ROW, BINS_PER_COL)) 
        { 
            #ifdef USE_AIE_ACCELERATION
            // Open Xilinx Device
            xrt::device device = xrt::device(DEVICE_ID);
            log_info("Device ID found: " + std::to_string(DEVICE_ID));

            // Load xclbin which includes PL and AIE graph
            log_info("Loading xclbin: \"" + xclbin_file + "\"");
            xrt::uuid xclbin_uuid = device.load_xclbin(xclbin_file);
            log_info("Success!");

            // Create drivers which handle buffer IO
            for(int i = 0; i < PARTIALS_GRAPH_COUNT; i++)
                partials_drivers[i].init(device, xclbin_uuid, i);

            //density_driver[0].init(device, xclbin_uuid, 0); // DCT graph
            //density_driver[1].init(device, xclbin_uuid, 1); // IDCT graph
            //density_driver[2].init(device, xclbin_uuid, 2); // IDXST graph
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

    Table info;
    info.add_row({"Version:", AIEPLACE_VERSION});
    info.format().hide_border();
    banner.add_row({info});
    banner.add_row({"VLSI global placement algorithm accelerated on AI Engines"});
    banner.add_row({}); // This line intentionally left blank

banner.format()
    .width(59)
    .hide_border()
    .font_color(Color::white)
    .font_align(FontAlign::left);
    
    banner.print(cout);
}

#define MIN_TOL 0.01
bool isClose(float a, float b)
{
    float diff = abs(abs(a) - abs(b));
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
    log("function", "Begin initializePlacement()");
    Table top;
    top.add_row(RowStream{} << "Initial Placement");
    Table data;
    data.add_row(RowStream{} << "Center" << target_pos.getX() << target_pos.getY());
    data.add_row(RowStream{} << "Min dist" << min_dist);
    data.add_row(RowStream{} << "Max dist" << max_dist); 
    top.add_row({data});
    top.format().font_align(FontAlign::center);
    log("INFO", top);

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

double prepare_compute_time = 0;
double prepare_actual_time = 0, receive_time= 0;
/*
 * @brief On AIEs, compute partial derivatives
 *
**/
#define GROUP_SIZE 1 // Size of the group of nets sent before waiting to receive results
void Placer::computeAllPartials_AIE()
{
    log("function", "Begin computeAllPartials_AIE()");
    // Start timer
    long start_partials = getTime();

    // for each packet specified in DataBase
    int packet_index = 0;
    while(packet_index < db.getPacketCount()) {
        int graphs_active = 0;
        // send a packet to each AIE graph
        long start_prep = getTime();
        std::thread partials_threads[PARTIALS_GRAPH_COUNT];
        for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
            //cout << "packet_index: " << packet_index << "\t < " << db.mv_packet[graph_index].size() << endl;
            if(packet_index < db.mv_packet[graph_index].size()) {
                //cout << "computePartials on graph " << db.mv_packet[graph_index][packet_index]->graph_index
                //    << "\t" << db.mv_packet[graph_index][packet_index]->contents[0].to_string();
                computePartials(db.mv_packet[graph_index][packet_index]);

                // Need to tell each thread starting offset, and packets_per_graph to know how far to go
                //partials_threads[graph_index] = std::thread([this, graph_index, packet_index]() {
                //    this->computePartials(db.mv_packet[graph_index][packet_index]);
                //});
                graphs_active++;
            }
        }

        // Join threads
        //for(int graph_index = 0; graph_index < graphs_active; graph_index++) {
        //    partials_threads[graph_index].join();
        //}

     prepare_actual_time += getInterval(start_prep, getTime());
     long start_receive = getTime();

        // receive output from each AIE graph
        for(int graph_index = 0; graph_index < graphs_active; graph_index++) {
            if(packet_index < db.mv_packet[graph_index].size()) {
                //cout << endl << "Receiving partials for graph " << graph_index << endl;
                receivePartials(db.mv_packet[graph_index][packet_index]);
                //partials_threads[graph_index] = std::thread([this, graph_index, packet_index]() {
                //    this->receivePartials(db.mv_packet[graph_index][packet_index]);
                //});
            }
        }

        // Join threads
        //for(int graph_index = 0; graph_index < graphs_active; graph_index++) {
        //    //cout << "Joining thread: " << graph_index << endl;
        //    partials_threads[graph_index].join();
        //}
     receive_time += getInterval(start_receive, getTime());

        packet_index++;
    }
//        for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//            cout << "Joining thread: " << graph_index << endl;
//            partials_threads[graph_index].join();
//        }

    // End timer and print
    double partials_compute_time = getInterval(start_partials, getTime());
    Table top;
    top.add_row(RowStream{} << "Prepare packet Total Thread compute time: " << prepare_compute_time);
    top.add_row(RowStream{} << "Prepare packet actual compute time: " << prepare_actual_time);
    top.add_row(RowStream{} << "Receive packet actual compute time: " << receive_time);
    top.add_row(RowStream{} << "Total time for AIE partials compute time: " << partials_compute_time << " sec (" << PARTIALS_GRAPH_COUNT  << " AIE cores used)");
    log("comms", top);
}

// Send a packet of coordinate data to the AIE partials computation graph
void Placer::computePartials(Packet* p)
{

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
    prepare_compute_time += getInterval(start, getTime());

    // send the data packet to PL (maybe as a thread?)
    partials_drivers[p->graph_index].send_packet(input_packet);

}

// Receive the result and place it into the database appropriately
void Placer::receivePartials(Packet* p)
{
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
            db.storeNetGroup(output_packet, pind.net_size, group_index*NETS_PER_GROUP);
        }
    }
}


/*
 * @brief On AIEs, compute Electric fields using 2D-DCT method
 *
**/
void Placer::computeElectricFields_AIE()
{
    log("function", "Begin computeElectricFields_AIE()");

    // Call AIE graph_driver to accelerate computation
    std::vector< std::vector<float> > rho = grid.getRho();
    std::vector< std::vector<float> > temp;

    //DEBUGGING: print out the rho matrix
    //for( int x_index = 0; x_index < BINS_PER_ROW; x_index++)
    //{
    //    for( int y_index = 0; y_index < BINS_PER_COL; y_index++)
    //    {
    //        cout << rho[x_index][y_index] << " ";
    //    }
    //    cout << endl;
    //}
    //cout << endl;

    float * input_data  = new float[2*BINS_PER_ROW]; 
    float * output_data = new float[2*BINS_PER_ROW];

    // Send the rho matrix into the AIE, one row at a time, for 1D-DCTs

    for(int row = 0; row < BINS_PER_COL; row++) {
        for(int col = 0; col < BINS_PER_ROW; col++) {
        input_data[2*col] = rho[row][col];
        input_data[2*col+1] = 0;
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
        for(int row = 0; row < BINS_PER_COL; row++) { // looping order performs DCT on columns
        input_data[2*row] = temp[row][col];
        input_data[2*row+1] = 0;
        }

        // Send data to DCT graph
        density_driver[0].send_packet(input_data);
        density_driver[0].receive_packet(output_data);

        //cout << endl << "DCT output:" << endl << std::setprecision(2);
        // Store the result a_uv transposed (for comparison)
        for(int row = 0; row < BINS_PER_COL; row++) {
            grid.getBin(row, col).a_uv = output_data[2*row];
            //cout << output_data[2*row] << " "; 
        }
        //cout << endl;

    }

    // Compute Ex
    temp.clear();
    // Setup input for IDCT
    double w_u, w_v, denom_inv;
    for(int row = 0; row < BINS_PER_COL; row++) { //looping params implement transpose!
        //cout << endl << "IDCT input to AIE:" << endl << std::setprecision(2);
        for(int col = 0; col < BINS_PER_ROW; col++) {
            if(row == 0 && col == 0) 
                { w_u = 0; w_v = 0; denom_inv = 0;} // for 0, 0 we avoid division by 0
            else {
                w_u = 2*M_PI*row / BINS_PER_COL;
                w_v = 2*M_PI*col / BINS_PER_ROW;
                denom_inv = 1 / (w_u*w_u + w_v*w_v);
            }
            input_data[2*col] = grid.getBin(row, col).a_uv * w_u * denom_inv;
            input_data[2*col+1] = 0;
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
        for(int row = 0; row < BINS_PER_COL; row++) { // looping order performs IDXST on columns
            input_data[2*row] = temp[row][col];
            input_data[2*row+1] = 0;
        }

        // Send data to IDXST graph
        density_driver[2].send_packet(input_data);
        density_driver[2].receive_packet(output_data);

        //cout << endl << "IDXST output:" << endl << std::setprecision(2);
        // Store the result Ex transposed (for comparison)
        for(int row = 0; row < BINS_PER_COL; row++) {
            grid.getBin(row, col).eField.x = output_data[2*row];
            //cout << output_data[2*row] << " "; 
        }
        //cout << endl;
    }

    // Compute Ey
    temp.clear();
    // Setup input for IDXST
    for(int row = 0; row < BINS_PER_COL; row++) { //looping params implement transpose!
        //cout << endl << "IDXST input to AIE:" << endl << std::setprecision(2);
        for(int col = 0; col < BINS_PER_ROW; col++) {
            if(row == 0 && col == 0) 
                { w_u = 0; w_v = 0; denom_inv = 0;} // for a(0, 0) we avoid division by 0 (remove dc component)
            else {
                w_u = 2*M_PI*row / BINS_PER_COL;
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
        for(int row = 0; row < BINS_PER_COL; row++) { // looping order performs IDCT on columns
            input_data[2*row] = temp[row][col];
            input_data[2*row+1] = 0;
        }

        // Send data to IDCT graph
        density_driver[1].send_packet(input_data);
        density_driver[1].receive_packet(output_data);

        //cout << endl << "IDCT output:" << endl << std::setprecision(2);
        // Store the result Ex transposed (for comparison)
        for(int row = 0; row < BINS_PER_COL; row++) {
            grid.getBin(row, col).eField.y = output_data[2*row];
            //cout << output_data[2*row] << " "; 
        }
        //cout << endl;
    }
}


/***************
 * CPU functions
****************/

/*
 * @brief On CPU, compute Electric fields using 2D-DCT method
 *
 * Results of the partial derivative computation are stored 
 * within each node's data members
**/
void Placer::computeAllPartials_CPU()
{
    // DEBUG: run a small subset of nets
    //auto nets = db.getNetsByDegree();
    //for(int i = 0; i < 8; i++)
    //{
    //    computeNetPartials_CPU(nets[8][i]);
    //    nets[8][i]->printTerms();
    //}

    long start_partials = getTime();
    for (auto item : db.getNetsByDegree()) {
        if(item.first < MIN_AIE_NET_SIZE || item.first > MAX_AIE_NET_SIZE) continue;
        for (Net* net_p : item.second) {
            computeNetPartials_CPU(net_p);
        }
    }

    double partials_compute_time = getInterval(start_partials, getTime());
    Table top;
    top.add_row(RowStream{} << "Total time for CPU partials compute time: " << partials_compute_time << " sec");
    log("comms", top);

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
    // DEBUG: stop at max net size for comparison to AIE computation
    if(net_p->getDegree() < 2 || net_p->getDegree() > MAX_AIE_NET_SIZE) return;
    //if(net_p->getDegree() != 8 ) return;

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

        node_p->terms_cpu.partials.x += partial_x;
        node_p->terms_cpu.partials.y += partial_y;
    }
}

/*
 * @brief On CPU, compute Electric fields using 2D-DCT method
 *
**/
void Placer::computeElectricFields_CPU()
{
    compute_a_uv_naive();
    compute_eField_naive();
}

void Placer::computeElectricFields_DCT()
{
    compute_a_uv_DCT();
    compute_eField_DCT();
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
    bool mismatch = false;
    Table mismatches;
    mismatches.add_row({"u", "v", "DCT result", "AIE result", "isClose"});
    for (int u = 0; u < grid.getBinsPerRow(); u++)
        for (int v = 0; v < grid.getBinsPerCol(); v++) {
            //if(!isClose(a_uv[u][v], grid.getBin(u, v).a_uv))
            {
                mismatch = true;
                bool close = isClose(a_uv[u][v], grid.getBin(u, v).a_uv);
                mismatches.add_row(RowStream{} << std::setprecision(2) << u << v << a_uv[u][v] << grid.getBin(u, v).a_uv << close);
            }
        }
    if(mismatch) {
        Table top;
        top.add_row(RowStream{} << "a_uv mismatch");
        top.add_row({mismatches});
        log_error(top);
    } else log_info("Density DCT computation: all a_uv terms match!");

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
        cout << endl << "IDCT input from CPU:" << endl << std::setprecision(2);
        for (int col_index = 0; col_index < num_rows; col_index++)
            cout << Ex[row_index][col_index] << " ";
        Ex[row_index] = IDCT_naive (Ex[row_index]);
        cout << endl << "IDCT output from CPU:" << endl << std::setprecision(2);
        for (int col_index = 0; col_index < num_rows; col_index++)
            cout << Ex[row_index][col_index] << " ";
        Ey[row_index] = IDXST_naive(Ey[row_index]);
    }
    cout << endl;

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
    bool mismatch = false;
    Table mismatches;
    mismatches.add_row({"u", "v", "DCT result", "CPU result", "isClose"});
    for (int u = 0; u < grid.getBinsPerRow(); u++)
        for (int v = 0; v < grid.getBinsPerCol(); v++) {
            //grid.getBin(u, v).a_uv = a_uv[u][v];
            //if(!isClose(Ex[u][v], grid.getBin(u, v).eField.x))
            {
                mismatch = true;
                bool close = isClose(Ex[u][v], grid.getBin(u, v).eField.x);
                mismatches.add_row(RowStream{} <<std::setprecision(2) <<  u << v << Ex[u][v] << grid.getBin(u, v).eField.x << close);
            }
        }
    if(mismatch) {
        Table top;
        top.add_row(RowStream{} << "eField.y mismatch");
        top.add_row({mismatches});
        log_error(top);
    } else log_info("Density DCT computation: all eField.x terms match!");

    Table mismatch_y;
    mismatch = false;
    mismatch_y.add_row({"u", "v", "DCT result", "CPU result", "isClose"});
    for (int u = 0; u < grid.getBinsPerRow(); u++)
        for (int v = 0; v < grid.getBinsPerCol(); v++) {
            //grid.getBin(u, v).a_uv = a_uv[u][v];
            //if(!isClose(Ex[u][v], grid.getBin(u, v).eField.x))
            {
                mismatch = true;
                bool close = isClose(Ey[u][v], grid.getBin(u, v).eField.y);
                mismatch_y.add_row(RowStream{} <<std::setprecision(2) <<  u << v << Ey[u][v] << grid.getBin(u, v).eField.y << close);
            }
        }
    if(mismatch) {
        Table top;
        top.add_row(RowStream{} << "eField.y mismatch");
        top.add_row({mismatch_y});
        log_error(top);
    } else log_info("Density DCT computation: all eField.y terms match!");
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
    log("function", "Begin computeOverlaps()");

    //for (auto item : db.getPins())
    //    grid.computeBinOverlaps(item.second);


    // DEBUG
    double total_node_area = 0;
    for (auto item : db.getComponents())
        total_node_area += item.second->getArea();
    //for (auto item : db.getPins())
    //    total_node_area += item.second->getArea();

    for (auto item : db.getComponents())
        grid.computeBinOverlaps(item.second);

    // DEBUGGING
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
    log("overlap", t);
}

/* To confirm that the AIE has performed a correct computaiton, this function
 * compares the results to the CPU computation result
 */
void Placer::comparePartialResults()
{
    int print_count = 0;
    log_info("Comparing Partial Results.");
    auto nodes_map = db.getComponents();
    for (auto const& item: nodes_map) {
        Node* np = item.second;
        if(np->terms_cpu.partials.isClose(np->partials_aie))
            continue;
        else {
            log_error("Terms do not match for node: " + np->getName());
            np->printPartials();
            if(print_count++ > 50) return;
        }
    }
}

void Placer::compareDensityResults()
{
}

void Placer::nudgeAllNodes()
{
    log("function", "Begin nudgeAllNodes()");
    for (auto item : db.getComponents())
        nudgeNode(item.second);
    // Pins are set in place and cannot be moved!
    //for (auto item : db.getPins())
    //    nudgeNode(item.second);

}

void Placer::nudgeNode(Node* node_p)
{
    XY electro_force;
    electro_force.clear(); // set XY to 0

    // for each bin that this node overlaps
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
    move.x = coeff * (electro_force.x - node_p->partials_aie.x );
    move.y = coeff * (electro_force.y - node_p->partials_aie.y );

    //cout << "learning_rate: " << learning_rate << "\tcoeff: " <<coeff<< endl;
    //cout << "electro_force.x: " << electro_force.x <<"\telectro_force.y: " << electro_force.y << endl;
    //if(move.x != move.x) // check if nan
    //{
    //    cout << "move.x: " << move.x << endl;
    //    node_p->printXY();
    //    cout << "coeff: " << coeff << endl;
    //    cout << "electro_force: " << electro_force.x << " : " << electro_force.y << endl;
    //    cout << "partials: " << node_p->partials_aie.x << " : " << node_p->partials_aie.y << endl;
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
    if (iteration % 1 == 0)
    {
        Table top;
        top.add_row(RowStream{} << "Iteration" << iteration);
        top.add_row(RowStream{} << "HPWL" << db.computeTotalWirelength());
        top.add_row(RowStream{} << "Overflow" << grid.computeTotalOverflow());
        top.column(0).format().font_align(FontAlign::right);
        top.column(1).format().font_align(FontAlign::left);
        log("DATA", top);
    }

    // every 10 iterations, export a table in markdown
    if (iteration % 10 == 0)
        ;

    // every 10 iterations, export an image
    #ifdef CREATE_VISUALIZATION
        if (iteration % 10 == 0)
            viz.drawPlacement(db, getOutputPath(), iteration);
    #endif
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
    log_space();
    log_info("AIEplace algorithm complete.");
    Table top;
    top.add_row({"AIEplace Run Statistics"});

    Table results;
    results.add_row({"Benchmark name", db.getBenchmarkName()});
    results.add_row(RowStream{} << "Iterations" << iteration);
    results.add_row(RowStream{} << "CPU runtime" << "####"/*CPU_runtime*/);
    results.add_row(RowStream{} << "AIE runtime" << "####"/*AIE_runtime*/);
    results.add_row(RowStream{} << "Final HPWL" << db.computeTotalWirelength());
    results.add_row(RowStream{} << "Final Overflow" << grid.computeTotalOverflow());
    results.column(0).format().font_align(FontAlign::right);
    results.column(1).format().font_align(FontAlign::left);

    Table hyperparams;
    hyperparams.add_row(RowStream{} << "gamma" << gamma);
    hyperparams.add_row(RowStream{} << "learning rate" << learning_rate );
    hyperparams.column(0).format().font_align(FontAlign::right);
    hyperparams.column(1).format().font_align(FontAlign::left);

    top.format().font_align(FontAlign::center);
    top.add_row({results});
    top.add_row({hyperparams});
    log("DATA", top);
    export_markdown(top, getOutputPath().append("statistics.md"));
    // TODO: export final image to same place as markdown results
    
    // generate image of final placement
    #ifdef CREATE_VISUALIZATION
        viz.drawPlacement(db, getOutputPath(), iteration);
    #endif
}

fs::path Placer::getOutputPath()
{
    std::time_t time = std::time(0);   // get time now
    std::tm* now = std::localtime(&time);

    std::stringstream timestream;
    timestream << "run_" << now->tm_hour << ":" << now->tm_min << "_" << now->tm_yday
            << "_" << std::to_string(now->tm_year+1900);

    fs::path dir = "results";
    dir.append(db.getBenchmarkName());
    dir.append(timestream.str());
    fs::create_directories(dir); // ensure this directory exists

    return dir;
}


AIEPLACE_NAMESPACE_END
