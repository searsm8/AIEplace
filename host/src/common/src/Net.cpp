
#include "Net.h"
#include "Node.h"
#include "IOPad.h"


AIEPLACE_NAMESPACE_BEGIN

struct Xgreater
{
    bool operator()( Node* l, Node* r ) {
        return l->getProbeX() > r->getProbeX();
    }
};

struct Xlesser
{
    bool operator()( Node* l, Node* r ) {
        return l->getProbeX() < r->getProbeX();
    }
};

struct Ygreater
{
    bool operator()( Node* l, Node* r ) {
        return l->getProbeY() > r->getProbeY();
    }
};

struct Ylesser
{
    bool operator()( Node* l, Node* r ) {
        return l->getProbeY() < r->getProbeY();
    }
};

/**
 * Sorts mv_nodes in descending order by X positions for feeding to AIE kernels
 */
/// @brief Sort mv_nodes in descending order by X position (for feeding to AIE kernels).
void Net::sortPositionsByX()
{
    std::sort(mv_nodes.begin(), mv_nodes.end(), Xgreater());
}

/// @brief Sort mv_nodes in descending order by Y position (for feeding to AIE kernels).
void Net::sortPositionsByY()
{
    std::sort(mv_nodes.begin(), mv_nodes.end(), Ygreater());
}

string Net::to_string()
{
    string s = m_name + ":\n";
    for (const NetPin& pin : mv_pins)
    {
        if(pin.node_p == nullptr)
        {
            Logger::log_error("nullptr found!");
            exit(1);
        }
        s += "\t" + pin.node_p->getName() + " (Pin " + pin.pin_name + ") : " + pin.node_p->next.probe_pos.to_string() + "\n";
    }

    return s;
}

/**
 * @brief Compute this net's wirelength by the named method.
 * @param method "HPWL" or "RSMT"
 * @return the net's wirelength
 */
position_type Net::computeWirelength(string method, bool at_probe)
{
    if (method == "HPWL")
        return computeWirelength_HPWL(at_probe);
    else if (method == "RSMT")
        return computeWirelength_RSMT();
    else
    {
        // Invalid, emit error
        Logger::log_error("No wirelength method named " + method);
        exit(1);
    }
}

/**
 * @brief Half-Perimeter Wirelength: width + height of the net's pin bounding box.
 * @return the net's HPWL
 */
position_type Net::computeWirelength_HPWL(bool at_probe)
{
    // CLAUDE CODE: one branch outside the loop, not per pin -- this runs over every net every
    // iteration and the predicate is loop-invariant.
    auto pin_pos = [at_probe](const NetPin& pin) {
        return at_probe ? pin.getProbePos() : pin.getPos();
    };
    Position first = pin_pos(mv_pins.front());
    float min_x = first.x, max_x = first.x;
    float min_y = first.y, max_y = first.y;
    for (const NetPin& pin : mv_pins) {
        Position p = pin_pos(pin);
        min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
    }
    return (max_x - min_x) + (max_y - min_y);
}

/**
 * @brief Rectilinear Steiner Minimum Spanning Tree wirelength.
 * @return the net's RSMT wirelength
 * @note Not yet implemented — returns 0.
 */
position_type Net::computeWirelength_RSMT()
{
    position_type RSMT = 0;
    return RSMT;
}

Box Net::getBoundingBox()
{
    float min_x = __FLT_MAX__, min_y = __FLT_MAX__;
    float max_x = -__FLT_MAX__, max_y = -__FLT_MAX__;
    for (const NetPin& pin : mv_pins) {
        Position p = pin.getPos();
        min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
    }
    return Box(min_x, min_y, max_x, max_y);
}

bool Net::hasIOPad()
{
    for(Node* node_p : mv_nodes) {
        if (IOPad* iopad_p = dynamic_cast<IOPad*>(node_p))
            return true;
    }
    return false;
}

bool Net::hasFixedNode()
{
    for(Node* node_p : mv_nodes) {
        if (node_p->getStatus() == FIXED)
            return true;
    }
    return false;
}

AIEPLACE_NAMESPACE_END