#include "AIEplace.h"
#include "DCT.h"
#include <cmath>

AIEPLACE_NAMESPACE_BEGIN
Placer::Placer(fs::path input_dir, std::string xclbin_file) : 
        db(DataBase(input_dir)), // TODO: Database initialization should be multithreaded?
        #ifdef CREATE_VISUALIZATION
        viz(Visualizer(db.getDieArea())),
        #endif
        grid(Grid(db.getDieArea(), BINS_PER_ROW, BINS_PER_COL)) 
        { 
            #ifdef USE_AIE_ACCELERATION
            // Open Xilinx Device
            std::cout << "Loading xclbin: " << xclbin_file << std::endl;
            xrt::device device = xrt::device(DEVICE_ID);
            std::cout << "Device ID " << DEVICE_ID << " found!" << std::endl;

            // Load xclbin which includes PL and AIE graph
            std::cout << "Loading XCL bin..." << endl;
            xrt::uuid xclbin_uuid = device.load_xclbin(xclbin_file);

            // Create drivers which handle buffer IO
            for(int i = 0; i < PARTIALS_GRAPH_COUNT; i++)
                partials_drivers[i].init(device, xclbin_uuid, i);

            //density_driver.init(device, xclbin_uuid);
            #endif
        }

void Placer::printVersionInfo()
{
    cout << endl << "AIEplace v0.0.1" << ": Pre-release" << endl;
    cout << "****************************" << endl;
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

    bool converged = false;
    while( !converged )
    {
        performIteration();
        if (iteration++ == 1)
            converged = true;
    }
}

/* @brief: perform an entire iteration of the ePlace algorithm.
*/
void Placer::performIteration()
{
    iterationReset();

    // Compute terms for HPWL partials
    computeAllPartials_CPU();
    computeAllPartials_AIE();

    // Compute Electric Fields in each bin
    computeOverlaps();
    //db.printOverlaps();
    //grid.printOverflows();

    //computeElectricFields_CPU(); // naive compute
    //computeElectricFields_DCT(); // on CPU also
    //computeElectricFields_AIE();
    //placer.grid.printElectricFields();

    // Perform iteration node movement
    nudgeAllNodes();
    printIterationResults();
}

/* @brief: Reset all nodes and nets in preparation for the next iteration.
*/
void Placer::iterationReset()
{
    grid.iterationReset();
    db.iterationReset();
}

/* @brief: initialize placement of all moveable nodes randomly,
 *          clustered about the target position
 * @param: target_pos: position around which nodes are spread
 * @param: min_dist: minimum distance from target_pos a node can appear
 * @param: max_dist: maximum distance from target_pos a node can appear
*/
void Placer::initializePlacement(Position<position_type> target_pos, int min_dist, int max_dist)
{
    cout << "initializePlacement(" << target_pos.to_string() << ", " << min_dist << ", " << max_dist << ");\n";
    // For each component that isn't fixed
    for (auto item : db.getComponents()) {
        // Choose a random position based on parameters
        // Different initial position "shapes" could help with performance?
        // e.g. maybe a donut shape would be good.
        int x_offset = min_dist + rand()%(max_dist-min_dist); // clustered around target
        if(rand()%2 == 1) x_offset *= -1; // 50% chance to negate
        int y_offset = min_dist + rand()%(max_dist-min_dist); // clustered around target
        if(rand()%2 == 1) y_offset *= -1; // 50% chance to negate
        //int x_offset = rand()%(grid.getDieWidth()) - grid.getDieWidth()/2; // Even Spread
        //int y_offset = rand()%(grid.getDieWidth()) - grid.getDieWidth()/2; // Even Spread
        Position<position_type> init_pos = target_pos + Position<position_type>(x_offset, y_offset);
        //cout << "Initial pos of " << item.second->getName() << ": " << init_pos.to_string() << "\tcomp#"<<component_count++<< endl;
        item.second->setPosition(init_pos);
    }

    printIterationResults();
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
                //receivePartials(db.mv_packet[graph_index][packet_index]);
                partials_threads[graph_index] = std::thread([this, graph_index, packet_index]() {
                    this->receivePartials(db.mv_packet[graph_index][packet_index]);
                });
            }
        }

        // Join threads
        for(int graph_index = 0; graph_index < graphs_active; graph_index++) {
            //cout << "Joining thread: " << graph_index << endl;
            partials_threads[graph_index].join();
        }
     receive_time += getInterval(start_receive, getTime());

        packet_index++;
    }
//        for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//            cout << "Joining thread: " << graph_index << endl;
//            partials_threads[graph_index].join();
//        }

    // End timer and print
    cout << "Prepare packet Total Thread compute time: " << prepare_compute_time << endl;
    cout << "Prepare packet actual compute time: " << prepare_actual_time << endl;
    cout << "Receive packet actual compute time: " << receive_time << endl;
    double partials_compute_time = getInterval(start_partials, getTime());
    cout << "Total time for AIE partials compute time: " << partials_compute_time << " sec (" << PARTIALS_GRAPH_COUNT  << " AIE cores used)" << endl;
}

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

//void Placer::computeAllPartials_AIE_old()
//{
//    // PRINT NUMBER OF NETS BY SIZE
//    //int large_net_count = 0;
//    //for(auto nets : nets_by_degree) {
//    //    if(nets.first <= 10)
//    //        cout << "Number of nets with size " << nets.first << ": " << nets.second.size() << endl;
//    //    else large_net_count += nets.second.size();
//    //}
//    //    cout << "Number of nets with size > 10: " << large_net_count << endl;
//    //cout << "input_data:" << endl;
//    //for(int i = 0; i < VEC_SIZE+4; i++) {
//    //    cout << "input_data[" << i << "]" <<input_data[i] << endl;
//    //    if((i+1)%4 == 0) cout << endl;
//    //}
//
//    // Use AIE graph_driver to send data to AIE accelerater
//    cout << "Time for AIEs to compute all partials on " << PARTIALS_GRAPH_COUNT << " compute units:" << endl;
//    cout << "GROUP_SIZE: " << GROUP_SIZE << endl;
//    for(int net_size = TEST_NET_SIZE; net_size <= TEST_NET_SIZE; net_size++) {
//    //for(int net_size = 2; net_size <= MAX_AIE_NET_SIZE; net_size++) {
//        // Start timer
//        long start_partials = getTime();
//        int num_nets = db.getNetCountOfDegree(net_size);
//        int nets_per_graph = ceil(float(num_nets) / PARTIALS_GRAPH_COUNT);
//        int net_groups_per_graph = ceil(float(nets_per_graph) / (NETS_PER_GROUP * GROUP_SIZE) ); // We send 4 nets of data in each net group 
//        // DEBUG: Info on nets and packets
//        cout << endl;
//        cout << "net_size: " << net_size << endl;
//        cout << "num_nets: " << num_nets << endl;
//        cout << "nets_per_graph: " << nets_per_graph << endl;
//        cout << "net_groups_per_graph: " << net_groups_per_graph << endl;
//
//
//        float * input_packet = new float[(LCM_BUFFSIZE+1)*VEC_SIZE]; // extra size for ctrl data
//        float * output_packet = new float[LCM_BUFFSIZE*VEC_SIZE];
//
//        // Prepare control data for packets to all graphs
//        for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//            float * ctrl_packet = new float[VEC_SIZE]; 
//            db.prepareCtrlPacket(input_packet, net_size, net_groups_per_graph*GROUP_SIZE); // added *GROUP_SIZE, this was WRONG!!@!@!
//        }
//
//
//        // Send GROUP_SIZE net groups of data to each partials graph
//        //for(int packet_index = 0; packet_index < GROUP_SIZE; packet_index++) {
//        //    for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//        //        int offset = NETS_PER_GROUP*(graph_index*net_groups_per_graph + packet_index);
//        //        db.prepareNextPartialsPacket(input_data, net_size, offset);
//
//        //        // Send packets of 8 floats from AIE graph until the net group is complete
//        //        for(int n = 0; n < net_size; n++)
//        //            partials_drivers[graph_index].send_packet(input_data + n*VEC_SIZE);
//        //    }
//        //}
//        
//        // Repeat send two net groups, receive two packets until all data has been sent
//        for(int packet_index = 0; packet_index < net_groups_per_graph; packet_index++) {
//            for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//                // Prepare net groups to fill up a packet of LCM_BUFFSIZE = 840
//                for(int g = 0; g < GROUP_SIZE; g++) {
//                    int offset = 0;//NETS_PER_GROUP*(graph_index*net_groups_per_graph + packet_index);
//                    db.prepareNetGroup(input_packet, net_size, offset);
//
//                    // Send packets of 8 floats from AIE graph until the net group is complete
//                    //for(int n = 0; n < net_size; n++)
//                    //    partials_drivers[graph_index].send_packet(input_data + n*VEC_SIZE);
//
//
//                    // Send packet of 840 nets of size net_size
//                    partials_drivers[graph_index].send_packet(input_packet);
//                }
//            }
//
//            for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//                for(int g = 0; g < GROUP_SIZE; g++) {
//                    // Receive packets of 8 floats from AIE graph until the net group is complete
//                    //for(int n = 0; n < net_size; n++) {
//                    //    partials_drivers[graph_index].receive_packet(output_data + n*VEC_SIZE);
//                    //    //cout << "received packet @ " << output_data + n*VEC_SIZE << endl;
//                    //}
//
//                    // Receive packet of 8 nets of size net_size
//                    partials_drivers[graph_index].receive_packet(output_packet);
//                    db.storeNetGroup(output_packet, net_size);
//                }
//            }
//        }
//
//        // Receive final two net groups form each partials graph
//        //for(int packet_index = 0; packet_index < GROUP_SIZE; packet_index++) {
//        //    for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//        //        for(int n = 0; n < net_size; n++) {
//        //            partials_drivers[graph_index].receive_packet(output_data + n*VEC_SIZE);
//        //        }
//        //        db.storePacket(output_data, net_size);
//        //    }
//        //}
//
///*
//        //packets_per_graph = 10;
//        // Start threads to handle each AIE PartialsGraph
//        std::thread partials_threads[PARTIALS_GRAPH_COUNT];
//        for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//            // Need to tell each thread starting offset, and packets_per_graph to know how far to go
//            partials_threads[graph_index] = std::thread([this, graph_index, net_size, packets_per_graph]() {
//                this->computePartials(graph_index, net_size, packets_per_graph);
//            });
//        }
//
//        for(int graph_index = 0; graph_index < PARTIALS_GRAPH_COUNT; graph_index++) {
//            cout << "Joining thread: " << graph_index << endl;
//            partials_threads[graph_index].join();
//        }
//*/
//
//        //if(db.hasMorePacketsToSend(net_size))
//        //    cout << "WARNING: Unsent packets detected for net_size = " << net_size << endl;
//        //// End timer and print
//        double partials_compute_time = getInterval(start_partials, getTime());
//        cout << "net_size = " << net_size << ": " << partials_compute_time << " sec" << endl;
//        cout << "DONE with net_size = " << net_size << endl;
//    }
//// DEBUG: Compare results with CPU version:
//    float max_diff = 0;
//    float total_diff = 0;
//    int nonzero_count = 0;
//    //cout << endl << "*&*&*&*&   AIE vs CPU RESULTS COMPARISON   *&*&*&*&" << endl;
//    auto m_nets = db.getNetsByDegree();
//    for(auto item : db.getNetsByDegree()) {
//        //cout << "RESULTS FOR NET_SIZE = " << item.first << endl;
//        if(item.first <= MAX_AIE_NET_SIZE)
//        for(Net * net : item.second) {
//            //cout << "NET: " << net->getName() << endl;
//            if(net->getDegree() <= 1 || net->getDegree() > MAX_AIE_NET_SIZE) continue;
//            for(Node * np : net->getNodes()) {
//                //cout << "Node " << np->getName();
//                //cout << "\tCPU result (X, Y): " << np->terms.partials.x << ", " << np->terms.partials.y;
//                //cout << "\tAIE result (X, Y): " << np->terms_aie.partials.x << ", " << np->terms_aie.partials.y;
//                //cout << endl;
//                float diff = np->terms.partials.x - np->terms_aie.partials.x;
//                total_diff += abs(diff);
//                //if(abs(diff) > abs(max_diff)) max_diff = diff;
//                if(abs(diff) > abs(max_diff)) {max_diff = diff; }
//                if(diff != 0 && np->terms.partials.x != 0 && np->terms_aie.partials.x != 0)
//                    nonzero_count++;
//                diff = np->terms.partials.y - np->terms_aie.partials.y;
//                total_diff += abs(diff);
//                if(abs(diff) > abs(max_diff)) {max_diff = diff; }
//            }
//        }
//    }
//    float avg_diff = total_diff / nonzero_count;
//    cout << "Avg diff: " << avg_diff << endl;
//    cout << "Max diff: " << max_diff << endl;
//// END DEBUG
//}

/*@brief: Compute some partials 
* @param graph_index: which driver to use to send and receive data
* @param net_size: The size of nets we are computing partials for
* @param packets_per_graph: The number of packets we send to this graph
*/
//void Placer::computePartials(int graph_index, int net_size, int packets_per_graph) {
//    bool debug = true;
//    if(debug) cout << "### Th" << graph_index << ": START ### net_size = " << net_size << endl;
//    // Send control data packet
//    float * ctrl_packet = new float[VEC_SIZE]; 
//    db.prepareCtrlPacket(ctrl_packet, net_size, packets_per_graph);
//    partials_drivers[graph_index].send_packet(ctrl_packet);
//
//    // Send all data packets for this Partials graph compute unit
//    float * input_data  = new float[net_size*VEC_SIZE]; 
//    for(int packet_index = 0; packet_index < packets_per_graph; packet_index++) {
//        if(debug) cout << "Th" << graph_index << ": packet_index = " << packet_index << endl;
//        // Gather input data to send to AIE graph
//        int offset = NETS_PER_GROUP*(graph_index*packets_per_graph + packet_index);
//        db.prepareNextPartialsPacket(input_data, net_size, offset); // make sure order is right!!
//        // Send coordinate data packet for this net
//        for(int n = 0; n < net_size; n++) {
//            // Send the packet of 8 floats to AIE graph
//            partials_drivers[graph_index].send_packet(input_data + n*VEC_SIZE);
//        }
//
//        // Retrieve the result from the AIE kernels
//        float * output_data = new float[net_size*VEC_SIZE];
//        for(int n = 0; n < net_size; n++) {
//            partials_drivers[graph_index].receive_packet(output_data + n*VEC_SIZE);
//            //partials_drivers[graph_index].print_info(); // INACCURATE!!!
//        }
//
//        // Store results in node terms.partials
//        db.storePacket(output_data, net_size);
//    }
//        if(debug) cout << "Th" << graph_index << ": Function end" << endl;
//}



/*
 * @brief On AIEs, compute Electric fields using 2D-DCT method
 *
**/
void Placer::computeElectricFields_AIE()
{
    // Call AIE graph_driver to accelerate computation
    std::vector< std::vector<float> > rho = grid.getRho();

    float * input_data  = new float[2*BINS_PER_ROW]; 
    float * output_data = new float[2*BINS_PER_ROW];
    for(int i = 0; i < BINS_PER_ROW; i++) {
        input_data[2*i] = rho[0][i];
        input_data[2*i+1] = 0;
    }

    // DEBUG: Print input data received
    //cout << endl << "AIE DCT input: " << endl;
    //for(int i = 0; i < BINS_PER_ROW; i++) {
    //    cout << input_data[2*i] << "\t";
    //    if((i+1)%16 == 0) cout << endl;
    //}
    //cout << endl;

    density_driver.send_packet(input_data);
    density_driver.receive_packet(output_data);

    // DEBUG: Print output data received
    //cout << endl << "AIE compute DCT result: " << endl;
    //for(int i = 0; i < BINS_PER_ROW; i++) {
    //    cout << output_data[2*i] << "\t";
    //    if((i+1)%16 == 0) cout << endl;
    //}
    //cout << endl;

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
    cout << endl << "Total time for CPU partials compute time: " << partials_compute_time << " sec" << endl;

    // DEBUG: Print partials results
    //
    //for ( auto item :  db.getComponents() )
    //{
    //    Node* node_p = item.second;
    //    cout << node_p->getName() << "\tsum_of_partials_x: " << node_p->terms.partials.x << endl;
    //    cout << node_p->getName() << "\tsum_of_partials_y: " << node_p->terms.partials.y << endl;

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
        node_p->terms.a.plus.x  = exp( (node_p->getX() - nodes.front()->getX()) / gamma);
        node_p->terms.a.minus.x = exp( (nodes.back()->getX() - node_p->getX()) / gamma);
    }

    // Y positions
    net_p->sortPositionsByY();
    nodes = net_p->getNodes();
    for (Node* node_p : nodes) {
        node_p->terms.a.plus.y  = exp( (node_p->getY() - nodes.front()->getY()) / gamma);
        node_p->terms.a.minus.y = exp( (nodes.back()->getY() - node_p->getY()) / gamma);
    }
}

void Placer::compute_bc_terms_CPU(Net* net_p)
{
    compute_a_terms_CPU(net_p);
    for (Node* node_p : net_p->getNodes()) {
        // compute b terms
        net_p->terms.b.plus.x  += node_p->terms.a.plus.x;
        net_p->terms.b.minus.x += node_p->terms.a.minus.x;
        net_p->terms.b.plus.y  += node_p->terms.a.plus.y;
        net_p->terms.b.minus.y += node_p->terms.a.minus.y;

        // compute c terms
        net_p->terms.c.plus.x  += node_p->terms.a.plus.x  * node_p->getX();
        net_p->terms.c.minus.x += node_p->terms.a.minus.x * node_p->getX();
        net_p->terms.c.plus.y  += node_p->terms.a.plus.y  * node_p->getY();
        net_p->terms.c.minus.y += node_p->terms.a.minus.y * node_p->getY();
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
        float partial_x = (( 1 + node_p->getX()/gamma) * net_p->terms.b.plus.x - (net_p->terms.c.plus.x / gamma)) 
                                    * (node_p->terms.a.plus.x / (net_p->terms.b.plus.x * net_p->terms.b.plus.x))
                         - (( 1 - node_p->getX()/gamma) * net_p->terms.b.minus.x + (net_p->terms.c.minus.x / gamma)) 
                                    * (node_p->terms.a.minus.x / (net_p->terms.b.minus.x * net_p->terms.b.minus.x));

        float partial_y = (( 1 + node_p->getY()/gamma) * net_p->terms.b.plus.y - (net_p->terms.c.plus.y / gamma)) 
                                    * (node_p->terms.a.plus.y / (net_p->terms.b.plus.y * net_p->terms.b.plus.y))
                         - (( 1 - node_p->getY()/gamma) * net_p->terms.b.minus.y + (net_p->terms.c.minus.y / gamma)) 
                                    * (node_p->terms.a.minus.y / (net_p->terms.b.minus.y * net_p->terms.b.minus.y));

        node_p->terms.partials.x += partial_x;
        node_p->terms.partials.y += partial_y;
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
    for (int u = 0; u < grid.getBinsPerRow(); u++) {
// cout << "Computing intermediate a at u = " << u << endl;
        for (int v = 0; v < grid.getBinsPerCol(); v++) {
// cout << "v = " << v << endl;
            float w_u = 2 * M_PI * u / grid.getBinsPerRow();
            float w_v = 2 * M_PI * v / grid.getBinsPerCol();

            // For each bin at (u, v) compute the intermediate term a
            float a_uv = 0;
            for (int x = 0; x < grid.getBinsPerRow(); x++) {
                for (int y = 0; y < grid.getBinsPerCol(); y++) {
                    a_uv += grid.getBin(x, y).overlap * cos(w_u*x) * cos(w_v*y);
                }
            }
            a_uv /= grid.getBinsPerCol() * grid.getBinsPerRow(); // 1 / M^2
            grid.getBin(u, v).a_uv= a_uv;
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
    //for (int i = 0; i < grid.getBinsPerRow(); i++) {
    //    cout << rho[0][i] << "\t";
    //    if((i+1)%16 == 0) cout << endl;
    //}
    //cout << endl;
    //cout << endl << "CPU compute 1D-DCT output:" << endl;
    //for (int i = 0; i < grid.getBinsPerRow(); i++) {
    //    cout << temp[0][i] << "\t";
    //    if((i+1)%16 == 0) cout << endl;
    //}
    //cout << endl;

    temp = transpose(temp);

    // Perform 1-D DCT on transposed matrix
    for (int col_index = 0; col_index < grid.getBinsPerRow(); col_index++)
        a_uv.push_back(DCT_naive(temp[col_index]));
    
    a_uv = transpose(a_uv);

    // add the computed intermediate term a_uv to all bins
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
        Ex[row_index] = IDCT_naive (Ex[row_index]);
        Ey[row_index] = IDXST_naive(Ey[row_index]);
    }

    Ex = transpose(Ex);
    Ey = transpose(Ey);

    // compute IDCT on all rows of Ey, and IDXST on all rows of Ex
    for (int row_index = 0; row_index < num_rows; row_index++) {
        Ex[row_index] = IDXST_naive (Ex[row_index]);
        Ey[row_index] = IDCT_naive(Ey[row_index]);
    }

    Ex = transpose(Ex);
    Ey = transpose(Ey);

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
    for (auto item : db.getComponents())
        grid.computeBinOverlaps(item.second);

    //for (auto item : db.getPins())
    //    grid.computeBinOverlaps(item.second);

    // DEBUGGING
    //float total_overlap = 0;
    //for (int col = 0; col < grid.getBinsPerRow(); col++) {
    //    for (int row = 0; row < grid.getBinsPerCol(); row++) {
    //        total_overlap += grid.getBin(col, row).overlap;
    //    }
    //}

    //float total_node_area = 0;
    //for (auto item : db.getComponents())
    //    total_node_area += item.second->getArea();
    //for (auto item : db.getPins())
    //    total_node_area += item.second->getArea();

    //cout << "total_node_area: " << total_node_area  << endl;
    //cout << "total_overlap: " << total_overlap << endl;
}


void Placer::nudgeAllNodes()
{
    for (auto item : db.getComponents())
        nudgeNode(item.second);
    // Pins are set in place and cannot be moved!
    //for (auto item : db.getPins())
    //    nudgeNode(item.second);

}

void Placer::nudgeNode(Node* node_p)
{
    XY wirelen_gradient = node_p->terms.partials;
    XY electro_force;
    electro_force.clear();

    // for each bin that this node overlaps
    for (BinOverlap b : node_p->getBinOverlaps()) {
        Bin* bin = b.bin;
        float overlap = b.overlap;
        // add electric force
        // What does ePlace do for this step?
        electro_force.x += bin->lambda * overlap/bin->bb.getArea() * bin->eField.x;
        electro_force.y += bin->lambda * overlap/bin->bb.getArea() * bin->eField.y;
    }

    XY move;
    float electro_weight = 0; // Ignore electro force
    move.x = learning_rate*grid.getDieWidth()  * (-wirelen_gradient.x + electro_weight*electro_force.x);
    move.y = learning_rate*grid.getDieHeight() * (-wirelen_gradient.y + electro_weight*electro_force.y);

    // Update the position of this node
    node_p->translate(move.x, move.y);

    // DEBUGGING
    //cout << "NudgeNode(): "<< node_p->getName() 
    //    << " grad(" << wirelen_gradient.x << ", " << wirelen_gradient.y << ")"
    //    << "\telectro(" << electro_force.x << ", " << electro_force.y << ")" << endl;
}

void Placer::printIterationResults()
{
    cout << "Iteration " << iteration << ":";
    cout << "\t" << "Total HPWL: " << db.computeTotalWirelength();
    cout << "\t\t" << "total overflow: " << grid.computeTotalOverflow();
    cout << endl;

    #ifdef CREATE_VISUALIZATION
        if (iteration % 10 == 0)
            viz.drawPlacement(db, iteration);
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

void Placer::printRunResults()
{
    cout << std::setprecision(2) << std::fixed;
    cout << "Completed ePlace algorithm." << endl;
    cout << "***RUN STATISTICS***" << endl;
    cout << "CPU runtime: " << /*CPU_runtime << */endl;
    cout << "AIE runtime: " << /*AIE_runtime << */endl;
    cout << "Final Wirelength: " << /*final_wirelength << */endl;
    cout << "Wirelength reduction ratio: " << /*final_wirelength / initial_wirelength << */ endl;
    cout << "Total Node Area: " << /*total_node_area <<*/endl;
    cout << "Total Overflow Area: " << /*total_overflow_area <<*/endl;


}
AIEPLACE_NAMESPACE_END
