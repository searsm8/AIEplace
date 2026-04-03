// Partials.cpp
// HPWL partial derivative computation functions
// Separated from AIEplace.cpp for better organization

#include "AIEplace.h"
#include <cmath>
#include <cassert>
#include <chrono>

AIEPLACE_NAMESPACE_BEGIN

void Placer::computeHpwlPartials()
{
    TIME_FUNCTION();

    // Clear probe_grad before accumulation, otherwise gradients compound across iterations.
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->next.probe_grad.clear();
    }
    for (auto filler : db.getFillers()) {
        filler->next.probe_grad.clear();
    }
    for (auto item : db.getIOPads()) {
        item.second->next.probe_grad.clear();
    }

    if(partials_method == "aie") {
        #ifdef USE_XILINX_XRT
            computeAllPartials_AIE();
        #else
            Logger::log_error("partials_method 'aie' requires XRT. Recompile with BUILD_XRT=1 or use 'cpu'/'simple'");
            exit(1);
        #endif
    }
    else if(partials_method == "cpu") {
        computeHpwlPartials_CPU();
    }
    else if(partials_method == "simple") {
        computeHpwlPartials_simple();
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

void Placer::computeHpwlPartials_AIE()
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

// Precompute exp(-d/gamma) lookup table for the simple HPWL gradient method.
// Called once at startup when partials_method == "simple".
void Placer::initHpwlLut()
{
    hpwl_lut_range = LUT_GAMMA_MULTIPLIER * gamma;
    hpwl_lut_size = int(hpwl_lut_range / LUT_STEP) + 2; // +2 for interpolation safety
    hpwl_lut.resize(hpwl_lut_size);
    for (int i = 0; i < hpwl_lut_size; i++) {
        float d = i * LUT_STEP;
        hpwl_lut[i] = exp(-d / gamma);
    }
    Logger::log_info("HPWL LUT initialized: " + std::to_string(hpwl_lut_size)
        + " entries, range=" + std::to_string(hpwl_lut_range)
        + ", gamma=" + std::to_string(gamma));
}

// Linearly interpolate into the precomputed exp(-d/gamma) LUT.
inline float Placer::lutLookup(float d) const
{
    float idx_f = d * inv_lut_step;
    int idx = int(idx_f);
    float frac = idx_f - idx;
    return hpwl_lut[idx] * (1.0f - frac) + hpwl_lut[idx + 1] * frac;
}

// LUT-based WA-HPWL gradient approximation.
// Uses the 2-node softmax approximation: for a node at distance d_max from the
// net's max edge and d_min from the min edge, the gradient is approximately:
//   grad = [exp(-d_max/γ) - exp(-d_min/γ)] / [1 + exp(-span/γ)]
// The exp values come from the precomputed LUT. Nodes far from both edges
// (distance > 5γ) get gradient ≈ 0 and are skipped entirely.
void Placer::computeHpwlPartials_simple()
{
    TIME_FUNCTION();

    const float range = hpwl_lut_range;

    for (Net* net_p : db.getNetsVector()) {
        const std::vector<NetPin>& pins = net_p->getPins();
        int net_size = net_p->getDegree();
        if (net_size <= 1) continue;

        // Find bounding box using pin positions (node + offset)
        float min_x = __FLT_MAX__, min_y = __FLT_MAX__;
        float max_x = -__FLT_MAX__, max_y = -__FLT_MAX__;
        for (const NetPin& pin : pins) {
            float px = pin.node->getProbeX() + pin.offset.x;
            float py = pin.node->getProbeY() + pin.offset.y;
            min_x = std::min(min_x, px); max_x = std::max(max_x, px);
            min_y = std::min(min_y, py); max_y = std::max(max_y, py);
        }

        float span_x = max_x - min_x;
        float span_y = max_y - min_y;

        // Normalization: 1/(1+exp(-span/γ)) — corrects for small-span nets
        // where the gradient magnitude should be < 1.
        float norm_x = (span_x < range) ? 1.0f / (1.0f + lutLookup(span_x)) : 1.0f;
        float norm_y = (span_y < range) ? 1.0f / (1.0f + lutLookup(span_y)) : 1.0f;

        for (const NetPin& pin : pins) {
            float x = pin.node->getProbeX() + pin.offset.x;
            float y = pin.node->getProbeY() + pin.offset.y;

            float d_max_x = max_x - x;
            float d_min_x = x - min_x;
            float d_max_y = max_y - y;
            float d_min_y = y - min_y;

            float plus_x  = (d_max_x < range) ? lutLookup(d_max_x) * norm_x : 0.0f;
            float minus_x = (d_min_x < range) ? lutLookup(d_min_x) * norm_x : 0.0f;

            float plus_y  = (d_max_y < range) ? lutLookup(d_max_y) * norm_y : 0.0f;
            float minus_y = (d_min_y < range) ? lutLookup(d_min_y) * norm_y : 0.0f;

            // Gradient accumulates onto the parent node
            pin.node->next.probe_grad.x += plus_x - minus_x;
            pin.node->next.probe_grad.y += plus_y - minus_y;
        }
    }
}

void Placer::computeHpwlPartials_CPU()
{
    TIME_FUNCTION();
    for (Net* net_p : db.getNetsVector()) {
        const std::vector<NetPin>& pins = net_p->getPins();
        int net_size = net_p->getDegree();

        if (net_size <= 1) continue;

        // find max and min x and y pin positions (node + offset)
        float min_x = __FLT_MAX__, min_y = __FLT_MAX__, max_x = -__FLT_MAX__, max_y = -__FLT_MAX__;
        for (const NetPin& pin : pins) {
            float px = pin.node->getProbeX() + pin.offset.x;
            float py = pin.node->getProbeY() + pin.offset.y;
            min_x = std::min(min_x, px); max_x = std::max(max_x, px);
            min_y = std::min(min_y, py); max_y = std::max(max_y, py);
        }

        // Compute A terms directly into our flat vector
        std::vector<Term> A(net_size);
        for (size_t i = 0; i < net_size; i++) {
            float px = pins[i].node->getProbeX() + pins[i].offset.x;
            float py = pins[i].node->getProbeY() + pins[i].offset.y;
            A[i].plus.x  = exp((px - max_x) * inv_gamma);
            A[i].minus.x = exp((min_x - px) * inv_gamma);
            A[i].plus.y  = exp((py - max_y) * inv_gamma);
            A[i].minus.y = exp((min_y - py) * inv_gamma);
        }

        // Compute B and C terms
        Term B, C;
        B.clear(); C.clear();
        for (size_t i = 0; i < net_size; i++) {
            float px = pins[i].node->getProbeX() + pins[i].offset.x;
            float py = pins[i].node->getProbeY() + pins[i].offset.y;
            B.plus.x  += A[i].plus.x;
            B.minus.x += A[i].minus.x;
            B.plus.y  += A[i].plus.y;
            B.minus.y += A[i].minus.y;
            C.plus.x  += A[i].plus.x  * px;
            C.minus.x += A[i].minus.x * px;
            C.plus.y  += A[i].plus.y  * py;
            C.minus.y += A[i].minus.y * py;
        }

        // Pre-compute common terms
        float bpx_sq_inv = 1.0f / (B.plus.x * B.plus.x);
        float bmx_sq_inv = 1.0f / (B.minus.x * B.minus.x);
        float bpy_sq_inv = 1.0f / (B.plus.y * B.plus.y);
        float bmy_sq_inv = 1.0f / (B.minus.y * B.minus.y);

        if(B.plus.x == 0 || B.minus.x == 0 || B.plus.y == 0 || B.minus.y == 0) {
            Logger::log_error("Zero value detected in B terms, cannot compute partials for net " + net_p->getName());
            Logger::log_error("B: " + B.to_string());
            Logger::log_error(net_p->to_string());
            for (size_t i = 0; i < net_size; i++)
                Logger::log_error("A[" + std::to_string(i) + "]: " + A[i].to_string() + " node: " + pins[i].node->getName());
            Logger::log_error("max_x: " + std::to_string(max_x) + " min_x: " + std::to_string(min_x) + " max_y: " + std::to_string(max_y) + " min_y: " + std::to_string(min_y));
            Logger::log_error("Probe positions:");
            for (size_t i = 0; i < net_size; i++) {
                float px = pins[i].node->getProbeX() + pins[i].offset.x;
                float py = pins[i].node->getProbeY() + pins[i].offset.y;
                Logger::log_error("Node " + pins[i].node->getName() + " pin_x: " + std::to_string(px) + " pin_y: " + std::to_string(py));
            }
        }
        assert(B.plus.x  != 0 && "B.plus.x is zero, cannot compute partials");
        assert(B.minus.x != 0 && "B.minus.x is zero, cannot compute partials");
        assert(B.plus.y  != 0 && "B.plus.y is zero, cannot compute partials");
        assert(B.minus.y != 0 && "B.minus.y is zero, cannot compute partials");

        // Compute partials and store — gradient accumulates onto parent node
        for (size_t i = 0; i < net_size; i++) {
            float px = pins[i].node->getProbeX() + pins[i].offset.x;
            float py = pins[i].node->getProbeY() + pins[i].offset.y;

            Gradient partial;
            partial.x = ((1 + px * inv_gamma) * B.plus.x - (C.plus.x * inv_gamma))
                      * (A[i].plus.x * bpx_sq_inv)
                    - ((1 - px * inv_gamma) * B.minus.x + (C.minus.x * inv_gamma))
                      * (A[i].minus.x * bmx_sq_inv);

            partial.y = ((1 + py * inv_gamma) * B.plus.y - (C.plus.y * inv_gamma))
                      * (A[i].plus.y * bpy_sq_inv)
                    - ((1 - py * inv_gamma) * B.minus.y + (C.minus.y * inv_gamma))
                      * (A[i].minus.y * bmy_sq_inv);

            //check for NaNs
            if(partial.x != partial.x || partial.y != partial.y) {
                Logger::log_error("NaN detected in partials for node " + pins[i].node->getName() + " in net " + net_p->getName());
                Logger::log_error("partial x: " + std::to_string(partial.x) + " y: " + std::to_string(partial.y));
                Logger::log_error("net size: " + std::to_string(net_size));
                Logger::log_error(net_p->to_string());
                Logger::log_error("A: " + A[i].to_string());
                Logger::log_error("B: " + B.to_string());
                Logger::log_error("C: " + C.to_string());
                exit(1);
            }

            pins[i].node->next.probe_grad += partial;
        }
    }
}

AIEPLACE_NAMESPACE_END
