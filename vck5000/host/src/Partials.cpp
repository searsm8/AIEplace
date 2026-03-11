// Partials.cpp
// HPWL partial derivative computation functions
// Separated from AIEplace.cpp for better organization

#include "AIEplace.h"
#include <cmath>
#include <cassert>
#include <chrono>

AIEPLACE_NAMESPACE_BEGIN

void Placer::computeAllPartials()
{
    db.clearPartials();

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
}

/***************
 * XRT/AIE ACCELERATION FUNCTIONS - VCK5000 only
 *
 * These functions are only compiled when USE_XILINX_XRT is defined.
 * They provide hardware-accelerated computation on Versal AI Engines via XRT.
 ****************/

#ifdef USE_XILINX_XRT

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
            if(packet_index < db.mv_packet[graph_index].size()) {
                partials_threads.emplace_back(&AIEplace::Placer::computePartials, this, db.mv_packet[graph_index][packet_index]);
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
                receivePartials(db.mv_packet[graph_index][packet_index]);
            }
        }
        Logger::updateFunctionStats("receiving_packets", t_receive.stop());
    }

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

    // receive the result data packet from PL
    float * output_packet = new float[OUTPUT_PACKET_SIZE];
    partials_drivers[p->graph_index].receive_packet(output_packet);

    // store it into database, updating node partials
    for(PacketIndex pind : p->contents) {
        for(int group_index = pind.group_start; group_index < pind.group_start + pind.group_count; ++group_index) {
            int nan_count  = db.storeNetGroup(output_packet, pind.net_size, group_index*NETS_PER_GROUP);
            if(nan_count > 0) {
                Logger::log_critical("NaN result detected...exiting");
                exit(1);
            }

        }
    }
    Logger::log_trace("END receivePartials(Packet* p)");
}

#endif // USE_XILINX_XRT


/***************
 * CPU FUNCTIONS - Always available
 *
 * These functions run on the host CPU and don't require XRT or VCK5000 hardware.
 ****************/

// Compute all partials using a table-based approach
// nodes far enough away are assigned partials of 1 or -1
// nodes in between are table look up
void Placer::computeAllPartials_simple()
{
    TIME_FUNCTION();
    Logger::log_trace("BEGIN computeAllPartials_simple()");

    auto& nets = db.getNetsVector();
    // Single-threaded computation - test baseline performance
    auto start_single = std::chrono::high_resolution_clock::now();

    // Process all nets sequentially
    for (Net* net_p : nets) {
        const std::vector<Node*>& nodes = net_p->getNodes();
        int net_size = net_p->getDegree();

        // Skip further processing for very small nets
        if (net_size <= 1) continue;

        // find max and min x and y probe positions
        float min_x = __FLT_MAX__, min_y = __FLT_MAX__, max_x = 0, max_y = 0;
        for (Node* node_p : nodes) {
            min_x = std::min(min_x, node_p->getProbeX());
            min_y = std::min(min_y, node_p->getProbeY());
            max_x = std::max(max_x, node_p->getProbeX());
            max_y = std::max(max_y, node_p->getProbeY());
        }

        for(size_t i = 0; i < net_size; i++) {
            float x = nodes[i]->getProbeX();
            float y = nodes[i]->getProbeY();

            // Compute partials using shortcut
            const int THRESHOLD = 10; // distance threshold for partials
            Gradient simple_partial;
            if (x < min_x + THRESHOLD) {
                simple_partial.x = (int(x - min_x) * 0.1f) - 1;
            } else if (x > max_x - THRESHOLD) {
                simple_partial.x = 1 - (int(max_x - x) * 0.1f);
            } else {
                simple_partial.x = 0;
            }

            if (y < min_y + THRESHOLD) {
                simple_partial.y = (int(y - min_y) * 0.1f) - 1;
            } else if (y > max_y - THRESHOLD) {
                simple_partial.y = 1 - (int(max_y - y) * 0.1f);
            } else {
                simple_partial.y = 0;
            }

            nodes[i]->next.probe_grad.x += simple_partial.x ;
            nodes[i]->next.probe_grad.y += simple_partial.y ;
        }
    }

    auto end_single = std::chrono::high_resolution_clock::now();
    auto duration_single = std::chrono::duration_cast<std::chrono::milliseconds>(end_single - start_single).count();
}

// Memory optimized computeAllPartials function
void Placer::computeAllPartials_CPU()
{
    TIME_FUNCTION();
    auto& nets = db.getNetsVector();

    auto start_single = std::chrono::high_resolution_clock::now();

    // Process all nets sequentially
    for (Net* net_p : nets) {
        const std::vector<Node*>& nodes = net_p->getNodes();
        int net_size = net_p->getDegree();

        // Skip further processing for very small nets
        if (net_size <= 1) continue;

        // Record starting index for this net's results
        size_t start_idx = all_partials.size();

        // find max and min x and y probe positions
        float min_x = __FLT_MAX__, min_y = __FLT_MAX__, max_x = 0, max_y = 0;
        for (Node* node_p : nodes) {
            min_x = std::min(min_x, node_p->getProbeX());
            min_y = std::min(min_y, node_p->getProbeY());
            max_x = std::max(max_x, node_p->getProbeX());
            max_y = std::max(max_y, node_p->getProbeY());
        }

        // Compute A terms directly into our flat vector
        std::vector<Term> A(net_size);
        for (size_t i = 0; i < net_size; i++) {
            A[i].plus.x  = exp((nodes[i]->getProbeX() - max_x) * inv_gamma);
            A[i].minus.x = exp((min_x - nodes[i]->getProbeX()) * inv_gamma);
            A[i].plus.y  = exp((nodes[i]->getProbeY() - max_y) * inv_gamma);
            A[i].minus.y = exp((min_y - nodes[i]->getProbeY()) * inv_gamma);
        }

        // Compute B and C terms
        Term B, C;
        B.clear(); C.clear();
        for (size_t i = 0; i < net_size; i++) {
            B.plus.x  += A[i].plus.x;
            B.minus.x += A[i].minus.x;
            B.plus.y  += A[i].plus.y;
            B.minus.y += A[i].minus.y;
            C.plus.x  += A[i].plus.x  * nodes[i]->getProbeX();
            C.minus.x += A[i].minus.x * nodes[i]->getProbeX();
            C.plus.y  += A[i].plus.y  * nodes[i]->getProbeY();
            C.minus.y += A[i].minus.y * nodes[i]->getProbeY();
        }

        // Pre-compute common terms
        float bpx_sq_inv = 1.0f / (B.plus.x * B.plus.x);
        float bmx_sq_inv = 1.0f / (B.minus.x * B.minus.x);
        float bpy_sq_inv = 1.0f / (B.plus.y * B.plus.y);
        float bmy_sq_inv = 1.0f / (B.minus.y * B.minus.y);

        assert(B.plus.x  != 0 && "B.plus.x is zero, cannot compute partials");
        assert(B.minus.x != 0 && "B.minus.x is zero, cannot compute partials");
        assert(B.plus.y  != 0 && "B.plus.y is zero, cannot compute partials");
        assert(B.minus.y != 0 && "B.minus.y is zero, cannot compute partials");

        // Compute partials and store in our flat vector
        for (size_t i = 0; i < net_size; i++) {
            float x = nodes[i]->getProbeX();
            float y = nodes[i]->getProbeY();

            Gradient partial;
            partial.x = ((1 + x * inv_gamma) * B.plus.x - (C.plus.x * inv_gamma))
                      * (A[i].plus.x * bpx_sq_inv)
                    - ((1 - x * inv_gamma) * B.minus.x + (C.minus.x * inv_gamma))
                      * (A[i].minus.x * bmx_sq_inv);

            partial.y = ((1 + y * inv_gamma) * B.plus.y - (C.plus.y * inv_gamma))
                      * (A[i].plus.y * bpy_sq_inv)
                    - ((1 - y * inv_gamma) * B.minus.y + (C.minus.y * inv_gamma))
                      * (A[i].minus.y * bmy_sq_inv);

            //check for NaNs
            if(partial.x != partial.x || partial.y != partial.y) {
                Logger::log_error("NaN detected in partials for node " + nodes[i]->getName() + " in net " + net_p->getName());
                Logger::log_error("partial x: " + std::to_string(partial.x) + " y: " + std::to_string(partial.y));
                Logger::log_error("net size: " + std::to_string(net_size));
                Logger::log_error(net_p->to_string());
                Logger::log_error("A: " + A[i].to_string());
                Logger::log_error("B: " + B.to_string());
                Logger::log_error("C: " + C.to_string());
                exit(1);
            }

            nodes[i]->next.probe_grad.x += partial.x ;
            nodes[i]->next.probe_grad.y += partial.y ;
        }
    }

    auto end_single = std::chrono::high_resolution_clock::now();
    auto duration_single = std::chrono::duration_cast<std::chrono::milliseconds>(end_single - start_single).count();
    Logger::log_detail("Sequential computation of partials on CPU took " + std::to_string(duration_single) + " ms");
}

void Placer::compute_a_terms_CPU(Net* net_p)
{
    // X positions
    net_p->sortPositionsByX();
    std::vector<Node*> nodes = net_p->getNodes();
    for (Node* node_p : nodes) {
        node_p->terms_cpu.a.plus.x  = exp( (node_p->getX() - nodes.front()->getX()) / gamma);
        node_p->terms_cpu.a.minus.x = exp( (nodes.back()->getX() - node_p->getX()) / gamma);
        assert(node_p->terms_cpu.a.plus.x <= 1 && "Invalid a+ computed!");
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
        compute_bc_terms_CPU(net_p);
        // Pre-compute common terms
        float inv_gamma = 1.0f / gamma;
        float bpx_sq_inv = 1.0f / (net_p->terms_cpu.b.plus.x * net_p->terms_cpu.b.plus.x);
        float bmx_sq_inv = 1.0f / (net_p->terms_cpu.b.minus.x * net_p->terms_cpu.b.minus.x);
        float bpy_sq_inv = 1.0f / (net_p->terms_cpu.b.plus.y * net_p->terms_cpu.b.plus.y);
        float bmy_sq_inv = 1.0f / (net_p->terms_cpu.b.minus.y * net_p->terms_cpu.b.minus.y);

        // Compute partials and store in our flat vector
        for (Node* node_p : net_p->mv_nodes) {
            float x = node_p->getX();
            float y = node_p->getY();

            Gradient partial;
            partial.x = ((1 + x * inv_gamma) * net_p->terms_cpu.b.plus.x - (net_p->terms_cpu.c.plus.x * inv_gamma))
                      * (node_p->terms_cpu.a.plus.x * bpx_sq_inv)
                    - ((1 - x * inv_gamma) * net_p->terms_cpu.b.minus.x + (net_p->terms_cpu.c.minus.x * inv_gamma))
                      * (node_p->terms_cpu.a.minus.x * bmx_sq_inv);

            partial.y = ((1 + y * inv_gamma) * net_p->terms_cpu.b.plus.y - (net_p->terms_cpu.c.plus.y * inv_gamma))
                      * (node_p->terms_cpu.a.plus.y * bpy_sq_inv)
                    - ((1 - y * inv_gamma) * net_p->terms_cpu.b.minus.y + (net_p->terms_cpu.c.minus.y * inv_gamma))
                      * (node_p->terms_cpu.a.minus.y * bmy_sq_inv);

            node_p->terms_cpu.partials.x += partial.x;
            node_p->terms_cpu.partials.y += partial.y;
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


/* @brief: For each node on net_p, compute partial derivative with respect to the net.
 *         Function written to be inherently thread-safe without the need for mutexes
 *         Results are written to the partials map
 */
void Placer::computeNetPartials_ThreadSafe(Net* net_p)
{
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
    vector<Term> A(net_size);
    for(size_t i = 0; i < net_size; i++) {
        A[i].plus.x  = exp( (nodes[i]->getX() - max_x) / gamma);
        A[i].minus.x = exp( (min_x - nodes[i]->getX()) / gamma);
        A[i].plus.y  = exp( (nodes[i]->getY() - max_y) / gamma);
        A[i].minus.y = exp( (min_y - nodes[i]->getY()) / gamma);
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
    }
    // partials will be accumulated with other nodes elsewhere
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
            continue;
        }
        else
        {
            error_count++;
            Logger::log_error("Terms DO NOT match for node " + np->getName()
                    + " -- CPU result: " + np->terms_cpu.partials.to_string()
                    + " -- AIE result: " + np->partials_aie.to_string());
        }
    }

    std::stringstream ss;
    ss << "errors: " << error_count << "\ttotal: " << total << "\tproportion: " << float(error_count)/float(total) << endl;
    Logger::log_error(ss.str());
}

AIEPLACE_NAMESPACE_END
