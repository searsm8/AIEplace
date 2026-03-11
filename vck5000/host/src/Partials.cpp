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

}

void Placer::computeAllPartials_CPU()
{
    TIME_FUNCTION();
    for (Net* net_p : db.getNetsVector()) {
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

            nodes[i]->next.probe_grad += partial;
        }
    }
}

AIEPLACE_NAMESPACE_END
