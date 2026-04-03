
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
void Net::sortPositionsByX()
{
    std::sort(mv_nodes.begin(), mv_nodes.end(), Xgreater());
}

/**
 * Sorts mv_nodes in descending order by Y positions for feeding to AIE kernels
 */
void Net::sortPositionsByY()
{
    std::sort(mv_nodes.begin(), mv_nodes.end(), Ygreater());
}

/**
 * Sort mv_nodes for AIE kernel execution
 * max_x, min_x, x, x, x...
 */
void Net::sortPositionsMaxMinX()
{
    sortPositionsByX();
    // Put the min value in 2nd position
    std::swap(mv_nodes.at(1), mv_nodes.back());

    // For AIE kernels, data format requires 1st element being max and 2nd element being min
    // other coordinates for nodes on the net can be in any order
}

/**
 * Sort mv_nodes for AIE kernel execution
 * max_y, min_y, y, y, y...
 */
void Net::sortPositionsMaxMinY()
{
    sortPositionsByY();
    // Put the min value in 2nd position
    std::swap(mv_nodes.at(1), mv_nodes.back());

    // For AIE kernels, data format requires 1st element being max and 2nd element being min
    // other coordinates for nodes on the net can be in any order
}

string Net::to_string()
{
    string s = m_name + ":\n";
    for (const NetPin& pin : mv_pins)
    {
        if(pin.node == nullptr)
        {
            cout << "nullptr found!" << endl;
            exit(1);
        }
        s += "\t" + pin.node->getName() + " (Pin " + pin.pin_name + ") : " + pin.node->next.probe_pos.to_string() + "\n";
    }

    return s;
}

/**
 * Compute the wirelength of this net, using HPWL or RSMT method
 * 
 * @param method: name of the method to use. Either "HPWL" or "RSMT"
 * 
 * @return: The wirelength of the net.
 */
position_type Net::computeWirelength(string method)
{
    if (method == "HPWL")
        return computeWirelength_HPWL();
    else if (method == "RSMT")
        return computeWirelength_RSMT();
    else
    {
        // Invalid, emit error
        cout << "ERROR: no wirelength method named " << method << endl;
        exit(1);
    }
}

/** 
 * Simple Half-Perimeter Wirelength (HPWL)
 * 
 * @return: Estimate of the wirelength for the net, computed using HPWL.
 */
position_type Net::computeWirelength_HPWL()
{
    float min_x = mv_pins.front().node->getX() + mv_pins.front().offset.x;
    float max_x = min_x;
    float min_y = mv_pins.front().node->getY() + mv_pins.front().offset.y;
    float max_y = min_y;
    for (const NetPin& pin : mv_pins) {
        float px = pin.node->getX() + pin.offset.x;
        float py = pin.node->getY() + pin.offset.y;
        min_x = std::min(min_x, px);
        min_y = std::min(min_y, py);
        max_x = std::max(max_x, px);
        max_y = std::max(max_y, py);
    }
    return (max_x - min_x) + (max_y - min_y);
}

/** 
 * Rectilinear Steiner Minimum Spanning Tree (RSMT)
 * 
 * @return: The wirelength of the net, computed using RSMT.
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
        float px = pin.node->getX() + pin.offset.x;
        float py = pin.node->getY() + pin.offset.y;
        min_x = std::min(min_x, px); max_x = std::max(max_x, px);
        min_y = std::min(min_y, py); max_y = std::max(max_y, py);
    }
    return Box(min_x, min_y, max_x, max_y);
}

bool Net::hasIOPad()
{
    for(Node* node : mv_nodes) {
        if (IOPad* iopad_ptr = dynamic_cast<IOPad*>(node))
            return true;
    }
    return false;
}

bool Net::hasFixedNode()
{
    for(Node* node : mv_nodes) {
        if (node->getStatus() == FIXED)
            return true;
    }
    return false;
}

AIEPLACE_NAMESPACE_END